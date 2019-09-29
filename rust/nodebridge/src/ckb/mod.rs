use std::time::{Duration, Instant};
use std::{str, thread};

use chrono::prelude::*;
use ckb_jsonrpc_types::BlockTemplate;
use ckb_types::prelude::{Builder, Entity, Pack};
use ckb_types::H256;
use failure::Error;
use futures::future::{loop_fn, Loop};
use futures::sync::mpsc::{channel, Sender};
use jsonrpc_core_client::{transports::http, RpcError};
use log::*;
use mysql_async::prelude::*;
use rdkafka::consumer::{Consumer, StreamConsumer};
use rdkafka::message::Message as KafkaMessage;
use rdkafka::producer::{FutureProducer, FutureRecord};
use rdkafka::topic_partition_list::Offset::Offset;
use rdkafka::{ClientConfig, TopicPartitionList};
use tokio::prelude::*;
use tokio::runtime::current_thread;
use tokio::timer::Delay;

use job::{MiningJob, Repository, SolvedShare};

use crate::ckb::rpc::Client;

mod job;
mod rpc;

enum Message {
    BlockTemplate(BlockTemplate),
    SolvedShare(SolvedShare),
}

impl From<BlockTemplate> for Message {
    fn from(block_template: BlockTemplate) -> Self {
        Message::BlockTemplate(block_template)
    }
}

impl From<SolvedShare> for Message {
    fn from(solved_share: SolvedShare) -> Self {
        Message::SolvedShare(solved_share)
    }
}

fn consume_solved_shares(kafka_brokers: &str, topic: &str, tx: Sender<Message>) {
    info!(
        "Creating solved share consumer to Kafka broker {} topic {}",
        kafka_brokers, topic
    );
    let consumer: StreamConsumer = ClientConfig::new()
        .set("bootstrap.servers", kafka_brokers)
        .set("enable.auto.commit", "false")
        .set("enable.partition.eof", "false")
        .set("group.id", "nodebridge-ckb")
        .set("message.timeout.ms", "5000")
        .create()
        .expect("Failed to solved share consumer");

    let mut tpl = TopicPartitionList::new();
    tpl.add_partition_offset(topic, 0, Offset(-2003));
    consumer
        .assign(&tpl)
        .expect("Failed to assign solved share topic");

    let task = consumer
        .start()
        .filter_map(|result| match result {
            Ok(msg) => Some(msg),
            Err(kafka_error) => {
                warn!("Error while receiving from Kafka: {:?}", kafka_error);
                None
            }
        })
        .for_each(move |msg| {
            let msg = msg.detach();
            if let Some(payload) = msg.payload() {
                if let Ok(solved_share) = serde_json::from_slice(payload) {
                    info!(
                        "Solved share received: {}",
                        str::from_utf8(payload).unwrap_or_default()
                    );
                    let task = tx
                        .clone()
                        .send(Message::SolvedShare(solved_share))
                        .and_then(|_| Ok(()))
                        .map_err(|e| error!("Failed to consume solved share: {}", e));
                    current_thread::spawn(task);
                }
            }
            Ok(())
        });
    current_thread::block_on_all(task).expect("Failed to start consuming solved shares");
}

fn produce_job(producer: &FutureProducer, topic: &str, job: &MiningJob) {
    if let Ok(job_str) = serde_json::to_string(job) {
        info!("Producing mining job: {}", &job_str);
        let record = FutureRecord::to(topic)
            .key("")
            .partition(0)
            .payload(&job_str);
        let task = producer
            .send(record, 0)
            .and_then(|_| Ok(()))
            .map_err(|e| error!("Failed to produce mining job: {}", e));
        tokio::spawn(task);
    }
}

fn execute_query(url: &str, stmt: String) {
    let pool = mysql_async::Pool::new(url);
    let count = 1;
    let task = loop_fn((count, pool, stmt), |(count, pool, stmt)| {
        {
            let pool = pool.clone();
            let stmt = stmt.clone();
            pool.get_conn().and_then(|conn| {
                info!("Executing query: {}", &stmt);
                conn.prep_exec(stmt, ())
            })
        }
        .then(
            move |result| -> Box<dyn Future<Item = _, Error = failure::Error> + Send> {
                match result {
                    Ok(_) => {
                        info!("Query is successfully executed");
                        pool.disconnect();
                        Box::new(future::ok(Loop::Break(())))
                    }
                    Err(e) => {
                        if count >= 5 {
                            Box::new(future::err(Error::from(e)))
                        } else {
                            error!("Failed to execute query: {}, retrying", e);
                            let fut = Delay::new(Instant::now() + Duration::from_secs(5))
                                .and_then(move |_| Ok(Loop::Continue((count + 1, pool, stmt))))
                                .map_err(|e| Error::from(e));
                            Box::new(fut)
                        }
                    }
                }
            },
        )
    })
    .map_err(|e| error!("Failed to execute query: {}", e));
    tokio::spawn(task);
}

fn create_block_record(
    url: &str,
    user_id: i32,
    worker_id: i64,
    worker_name: &str,
    height: u64,
    block_hash: &H256,
    pow_hash: &H256,
    nonce: u64,
    parent_hash: &H256,
    difficulty: u32,
) {
    let stmt = format!(
        "INSERT INTO `found_blocks` (`puid`,`worker_id`,`worker_full_name`,`height`,`hash`,`hash_no_nonce`,`nonce`,`prev_hash`,`network_diff`,`created_at`) VALUES({},{},'0x{}',{},'0x{}','0x{}','0x{:016x}','0x{}',{},'{}')",
        user_id,
        worker_id,
        worker_name,
        height,
        block_hash,
        pow_hash,
        nonce,
        parent_hash,
        difficulty,
        Utc::now().format("%F %T"),
    );
    execute_query(url, stmt);
}

fn update_block_reward(url: &str, block_hash: &H256, block_reward: &Result<u64, RpcError>) {
    let update = match block_reward {
        Ok(reward) => format!("`rewards` = {}", reward),
        Err(e) => format!("`rpc_error` = `{}`", e),
    };
    let stmt = format!(
        "UPDATE found_blocks SET {} WHERE `hash` = '0x{}'",
        &update, &block_hash,
    );
    execute_query(url, stmt);
}

fn submit_block(rpc_client: Client, job: &MiningJob, solved_share: &SolvedShare, db_url: String) {
    let header = job
        .block
        .header()
        .as_builder()
        .nonce(solved_share.nonce.pack())
        .build();
    let block = job
        .block
        .clone()
        .as_builder()
        .header(header)
        .build()
        .into_view();
    let hash = H256::from_slice(&block.hash().raw_data()).unwrap_or_default();

    create_block_record(
        &db_url,
        solved_share.user_id,
        solved_share.worker_id,
        &solved_share.worker_name,
        solved_share.height,
        &hash,
        &solved_share.pow_hash,
        solved_share.nonce,
        &job.parent_hash,
        job.compact_target,
    );

    info!("Submitting block {}", &hash);
    let task = rpc_client
        .submit_block(job.work_id, block.data().into())
        .then(move |result| match result {
            Ok(hash_returned) => {
                match hash_returned {
                    Some(_) => {
                        info!("Submitted block {}", &hash);
                        let fut = rpc_client
                            .get_cellbase_output_capacity_details(hash.clone())
                            .then(move |result| match result {
                                Ok(reward) => {
                                    update_block_reward(
                                        &db_url,
                                        &hash,
                                        &Ok(reward.unwrap_or_default().total.into()),
                                    );
                                    Ok(())
                                }
                                Err(e) => {
                                    update_block_reward(&db_url, &hash, &Err(e));
                                    Err(())
                                }
                            });
                        tokio::spawn(fut);
                        Ok(())
                    }
                    None => {
                        error!("Failed to submit block {}", &hash);
                        Err(())
                    }
                }
            }
            Err(e) => {
                error!("Failed to submit block {}: {}", &hash, e);
                update_block_reward(&db_url, &hash, &Err(e));
                Err(())
            }
        });
    tokio::spawn(task);
}

fn handle_messages(
    kafka_brokers: &str,
    job_topic: String,
    rpc_client: Client,
    db_url: String,
) -> Sender<Message> {
    info!(
        "Creating job producer to Kafka broker {} topic {}",
        kafka_brokers, &job_topic
    );
    let producer: FutureProducer = ClientConfig::new()
        .set("bootstrap.servers", kafka_brokers)
        .set("message.timeout.ms", "5000")
        .create()
        .expect("Failed to create job producer");

    let mut repository = Repository::new();
    let (tx, rx) = channel::<Message>(256);
    let task = rx.for_each(move |msg| match msg {
        Message::BlockTemplate(block_template) => {
            produce_job(
                &producer,
                &job_topic,
                &repository.on_block_template(block_template),
            );
            Ok(())
        }
        Message::SolvedShare(solved_share) => {
            if let Some(job) = repository.on_solved_share(&solved_share) {
                submit_block(rpc_client.clone(), job, &solved_share, db_url.clone());
            } else {
                error!(
                    "Failed to find work for solved share {} nonce {}",
                    solved_share.pow_hash, solved_share.nonce
                );
            }
            Ok(())
        }
    });
    tokio::spawn(task);
    tx
}

pub fn run(
    rpc_addr: &str,
    rpc_interval: u64,
    kafka_brokers: String,
    job_topic: String,
    solved_share_topic: String,
    db_url: String,
) {
    info!("Connecting to CKB node {}", rpc_addr);
    let fut = http::connect(rpc_addr)
        .and_then(move |rpc_client: rpc::Client| {
            let tx = handle_messages(&kafka_brokers, job_topic, rpc_client.clone(), db_url);
            {
                let tx = tx.clone();
                thread::spawn(move || {
                    consume_solved_shares(&kafka_brokers, &solved_share_topic, tx)
                })
            };

            loop_fn(
                (rpc_client, rpc_interval, tx),
                |(rpc_client, rpc_interval, tx)| {
                    rpc_client.get_block_template().and_then(move |result| {
                        info!(
                            "RPC get_block_template result: {}",
                            serde_json::to_string(&result).unwrap_or_default()
                        );

                        let task = tx
                            .clone()
                            .send(Message::BlockTemplate(result))
                            .and_then(|_| Ok(()))
                            .map_err(|e| error!("Failed to consume block template: {}", e));
                        tokio::spawn(task);

                        Delay::new(Instant::now() + Duration::from_millis(rpc_interval))
                            .and_then(move |_| Ok(Loop::Continue((rpc_client, rpc_interval, tx))))
                            .map_err(|e| RpcError::Other(Error::from(e)))
                    })
                },
            )
        })
        .map_err(|e| error!("RPC client error: {}", e));
    tokio::run(fut);
}

use std::str::from_utf8;
use std::time::Duration;

use ckb_jsonrpc_types::BlockTemplate;
use ckb_types::prelude::*;
use ckb_types::H256;
use futures::compat::Future01CompatExt;
use jsonrpc_core_client::transports::http;
use log::*;
use mysql_async::prelude::*;
use rdkafka::consumer::{Consumer, StreamConsumer};
use rdkafka::message::Message as KafkaMessage;
use rdkafka::producer::{FutureProducer, FutureRecord};
use rdkafka::topic_partition_list::Offset;
use rdkafka::util::Timeout;
use rdkafka::{ClientConfig, TopicPartitionList};
use tokio::spawn;
use tokio::sync::mpsc::{Receiver, Sender};
use tokio::time::delay_for;

use chrono::Utc;
use futures::StreamExt;

mod job;
mod rpc;

use job::*;
use rpc::*;

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

fn consume_solved_shares(kafka_brokers: String, topic: String, mut tx: Sender<Message>) {
    spawn(async move {
        info!(
            "Creating solved share consumer to Kafka broker {} topic {}",
            kafka_brokers, topic
        );
        let consumer: StreamConsumer = ClientConfig::new()
            .set("bootstrap.servers", &kafka_brokers)
            .set("enable.auto.commit", "false")
            .set("enable.partition.eof", "false")
            .set("group.id", "nodebridge-ckb")
            .set("message.timeout.ms", "5000")
            .create()
            .expect("Failed to solved share consumer");

        let mut tpl = TopicPartitionList::new();
        tpl.add_partition_offset(&topic, 0, Offset::Offset(-2003));
        consumer
            .assign(&tpl)
            .expect("Failed to assign solved share topic");

        let mut stream = consumer.start();
        while let Some(result) = stream.next().await {
            match result {
                Ok(msg) => match msg.detach().payload() {
                    Some(payload) => {
                        info!(
                            "Solved share received: {}",
                            from_utf8(payload).unwrap_or_default()
                        );
                        match serde_json::from_slice::<SolvedShare>(payload) {
                            Ok(solved_share) => {
                                tx.send(Message::from(solved_share))
                                    .await
                                    .unwrap_or_else(|e| {
                                        error!("Failed to send solved share for processing: {}", e);
                                    });
                            }
                            Err(e) => {
                                error!("Failed to deserialize solved share: {}", e);
                            }
                        }
                    }
                    None => {
                        error!("No payload for solved share message");
                    }
                },
                Err(e) => {
                    warn!("Error while receiving from Kafka: {:?}", e);
                }
            }
        }
    });
}

fn produce_job(producer: FutureProducer, topic: String, job: MiningJob) {
    match serde_json::to_string(&job) {
        Ok(job_str) => {
            spawn(async move {
                info!("Producing mining job: {}", &job_str);
                let record = FutureRecord::to(&topic)
                    .key("")
                    .partition(0)
                    .payload(&job_str);
                match producer.send(record, Timeout::Never).await {
                    Ok((partition, offset)) => {
                        info!(
                            "Mining job sent to partition {} offset {}",
                            partition, offset
                        );
                    }
                    Err((e, _)) => {
                        error!("Failed to produce mining job: {}", e);
                    }
                }
            });
        }
        Err(e) => {
            error!("Failed to serialize mining job: {}", e);
        }
    }
}

fn execute_query(url: String, stmt: String) {
    spawn(async move {
        let pool = mysql_async::Pool::new(url);
        let delay = 5u64;
        for count in 0..5 {
            if count != 0 {
                delay_for(Duration::from_secs(delay)).await;
            }

            match pool.get_conn().await {
                Ok(mut conn) => {
                    info!("Executing query: {}", &stmt);
                    match conn.query_drop(&stmt).await {
                        Ok(_) => {
                            info!("Query is successfully executed");
                            break;
                        }
                        Err(e) => {
                            error!(
                                "Failed to execute query: {}, retrying after {} seconds",
                                e, delay
                            );
                        }
                    }
                }
                Err(e) => {
                    error!(
                        "Failed to get connection: {}, retrying after {} seconds",
                        e, delay
                    );
                }
            }
        }
    });
}

fn create_block_record(
    url: String,
    user_id: i32,
    worker_id: i64,
    worker_name: &str,
    height: u64,
    block_hash: &H256,
    pow_hash: &H256,
    nonce: u128,
    parent_hash: &H256,
    compact_target: &u32,
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
        compact_target,
        Utc::now().format("%F %T"),
    );
    execute_query(url, stmt);
}

fn submit_block(rpc_client: Client, job: MiningJob, solved_share: SolvedShare, db_url: String) {
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
        db_url,
        solved_share.user_id,
        solved_share.worker_id,
        &solved_share.worker_name,
        solved_share.height,
        &hash,
        &solved_share.pow_hash,
        solved_share.nonce,
        &job.parent_hash,
        &job.compact_target,
    );

    spawn(async move {
        info!("Submitting block {}", &hash);
        match rpc_client
            .submit_block(job.work_id, block.data().into())
            .await
        {
            Ok(hash_returned) => {
                info!("Submit block {} returned {}", &hash, hash_returned);
            }
            Err(e) => {
                error!("Failed to submit block {}: {}", &hash, e);
            }
        }
    });
}

fn handle_messages(
    kafka_brokers: String,
    job_topic: String,
    rpc_client: Client,
    db_url: String,
    mut rx: Receiver<Message>,
) {
    spawn(async move {
        info!(
            "Creating job producer to Kafka broker {} topic {}",
            &kafka_brokers, &job_topic
        );
        let producer: FutureProducer = ClientConfig::new()
            .set("bootstrap.servers", &kafka_brokers)
            .set("message.timeout.ms", "5000")
            .create()
            .expect("Failed to create job producer");

        let mut repository = Repository::new();
        while let Some(msg) = rx.next().await {
            match msg {
                Message::BlockTemplate(block_template) => {
                    produce_job(
                        producer.clone(),
                        job_topic.clone(),
                        repository.on_block_template(block_template),
                    );
                }
                Message::SolvedShare(solved_share) => {
                    if let Some(job) = repository.on_solved_share(&solved_share) {
                        submit_block(
                            rpc_client.clone(),
                            job.clone(),
                            solved_share,
                            db_url.clone(),
                        );
                    } else {
                        error!(
                            "Failed to find work for solved share {} nonce {}",
                            solved_share.pow_hash, solved_share.nonce
                        );
                    }
                }
            }
        }
    });
}

pub fn run(
    rpc_addr: String,
    rpc_interval: u64,
    kafka_brokers: String,
    job_topic: String,
    solved_share_topic: String,
    db_url: String,
) {
    tokio_compat::run_std(async move {
        info!("Connecting to CKB node {}", rpc_addr);
        let result = http::connect::<Client>(&rpc_addr).compat().await;
        let rpc_client = result.expect("Failed to connect to CKB node");
        let (mut tx, rx) = tokio::sync::mpsc::channel::<Message>(256);
        handle_messages(
            kafka_brokers.clone(),
            job_topic.clone(),
            rpc_client.clone(),
            db_url.clone(),
            rx,
        );
        consume_solved_shares(kafka_brokers, solved_share_topic, tx.clone());
        loop {
            match rpc_client.get_block_template().await {
                Ok(block_template) => {
                    tx.send(Message::from(block_template))
                        .await
                        .unwrap_or_else(|e| {
                            error!("Failed to send block template for processing: {}", e)
                        });
                }
                Err(e) => {
                    error!("Failed to get block template: {}", e);
                }
            }
            delay_for(Duration::from_millis(rpc_interval)).await;
        }
    });
}

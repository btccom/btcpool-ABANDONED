mod ckb;

use structopt::StructOpt;

#[derive(StructOpt)]
#[structopt(
    name = "nodebridge",
    about = "Bridge between the mining pool and the blockchain node",
    rename_all = "snake_case"
)]
enum Opt {
    #[structopt(name = "ckb", about = "Node bridge for CKB network")]
    Ckb {
        /// RPC address in <host>:<port> format
        #[structopt(long)]
        rpc_addr: String,
        /// RPC interval in milliseconds
        #[structopt(long, default_value = "3000")]
        rpc_interval: u64,
        #[structopt(long)]
        /// Kafka brokers in comma separated <host>:<port> format
        kafka_brokers: String,
        /// Job topic name
        #[structopt(long)]
        job_topic: String,
        /// Solved share topic name
        #[structopt(long)]
        solved_share_topic: String,
        /// Database URL in mysql://<username>:<password>@<host>:<port>/<table> format
        #[structopt(long)]
        db_url: String,
    },
}

fn main() {
    env_logger::init();

    let opt = Opt::from_args();
    match opt {
        Opt::Ckb {
            rpc_addr,
            rpc_interval,
            kafka_brokers,
            job_topic,
            solved_share_topic,
            db_url,
        } => ckb::run(
            rpc_addr,
            rpc_interval,
            kafka_brokers,
            job_topic,
            solved_share_topic,
            db_url,
        ),
    }
}

use ckb_jsonrpc_types::BlockTemplate;
use ckb_types::packed::Block;
use ckb_types::{
    prelude::*,
    utilities::compact_to_target,
};
use ckb_types::{H256, U256};
use serde::{Deserialize, Serialize};
use std::collections::{BTreeMap, HashMap};
use std::time::Instant;

#[derive(Clone, Serialize)]
pub struct MiningJob {
    pub work_id: u64,
    pub height: u64,
    pub timestamp: u64,
    pub target: U256,
    pub parent_hash: H256,
    pub pow_hash: H256,
    #[serde(skip)]
    pub compact_target: u32,
    #[serde(skip)]
    pub job_time: Instant,
    #[serde(skip)]
    pub block: Block,
}

impl From<BlockTemplate> for MiningJob {
    fn from(block_template: BlockTemplate) -> Self {
        let work_id = block_template.work_id.into();
        let height = block_template.number.into();
        let timestamp = block_template.current_time.into();
        let compact_target = block_template.compact_target.into();
        let parent_hash = block_template.parent_hash.clone();
        let block: Block = block_template.into();
        let (target, _,) = 
            compact_to_target(block.header().raw().compact_target().unpack());
        let pow_hash =
            H256::from_slice(&block.header().calc_pow_hash().raw_data()).unwrap_or_default();
        let job_time = Instant::now();
        MiningJob {
            work_id,
            height,
            timestamp,
            target,
            parent_hash,
            pow_hash,
            compact_target,
            job_time,
            block,
        }
    }
}

#[derive(Deserialize)]
pub struct SolvedShare {
    pub work_id: u64,
    pub height: u64,
    pub timestamp: u64,
    pub pow_hash: H256,
    pub target: U256,
    pub nonce: u128,
    pub job_id: u64,
    #[serde(rename = "userId")]
    pub user_id: i32,
    #[serde(rename = "workerId")]
    pub worker_id: i64,
    #[serde(rename = "workerFullName")]
    pub worker_name: String,
}

pub struct Repository {
    jobs: HashMap<H256, MiningJob>,
    hashes: BTreeMap<Instant, H256>,
}

impl Repository {
    pub fn new() -> Self {
        Repository {
            jobs: HashMap::new(),
            hashes: BTreeMap::new(),
        }
    }

    pub fn on_block_template(&mut self, block_template: BlockTemplate) -> MiningJob {
        let job: MiningJob = block_template.into();
        self.insert_job(&job);
        self.clean_jobs();
        job
    }

    pub fn on_solved_share(&self, solved_share: &SolvedShare) -> Option<&MiningJob> {
        self.jobs.get(&solved_share.pow_hash)
    }

    fn insert_job(&mut self, job: &MiningJob) {
        if let Some(old_hash) = self
            .hashes
            .insert(job.job_time.clone(), job.pow_hash.clone())
        {
            self.jobs.remove(&old_hash);
        }
        if let Some(old_job) = self.jobs.insert(job.pow_hash.clone(), job.clone()) {
            self.hashes.remove(&old_job.job_time);
        }
    }

    fn clean_jobs(&mut self) {
        let removables: Vec<(Instant, H256)> = self
            .hashes
            .iter()
            .take(self.hashes.len().saturating_sub(256))
            .map(|(job_time, hash)| (job_time.clone(), hash.clone()))
            .collect();
        removables.iter().for_each(|(job_time, hash)| {
            self.hashes.remove(job_time);
            self.jobs.remove(hash);
        })
    }
}

use ckb_jsonrpc_types::{Block, BlockReward, BlockTemplate};
use ckb_types::H256;
use futures::future::Future;
use jsonrpc_core::Value;
use jsonrpc_core_client::{RpcChannel, RpcError, TypedClient};

#[derive(Clone)]
pub(crate) struct Client(TypedClient);

impl From<RpcChannel> for Client {
    fn from(channel: RpcChannel) -> Self {
        Client(channel.into())
    }
}

impl Client {
    pub fn get_block_template(&self) -> impl Future<Item = BlockTemplate, Error = RpcError> {
        self.0
            .call_method("get_block_template", "BlockTemplate", Value::Null)
    }

    pub fn submit_block(
        &self,
        work_id: u64,
        block: Block,
    ) -> impl Future<Item = H256, Error = RpcError> {
        self.0
            .call_method("submit_block", "H256", (work_id.to_string(), block))
    }

    pub fn get_cellbase_output_capacity_details(
        &self,
        hash: H256,
    ) -> impl Future<Item = Option<BlockReward>, Error = RpcError> {
        self.0.call_method(
            "get_cellbase_output_capacity_details",
            "BlockReward",
            (hash,),
        )
    }
}

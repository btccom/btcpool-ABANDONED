Prometheus Metrics
==================

Several Prometheus metrics has been added to BTCPool components to allow better online monitoring. To enable the metrics within a BTCPool component, some new items has to be added to the component configurations (by default it is not enabled).

```
prometheus = {
  # whether prometheus exporter is enabled
  enabled = true
  # address for prometheus exporter to bind
  address = "0.0.0.0"
  # port for prometheus exporter to bind
  port = 9100
  # path of the prometheus exporter url
  path = "/metrics"
};
```

## Component Metrics

### ssever

Component sserver provides the following metrics

* `sserver_identity` This is the numeric identity of sserver. For servers of the same coin, this identity shall be uniquely assigned (we recommend to use the automatic assignment via ZooKeeper).
* `sserver_sessions_total` The total session count of sserver, per chain and status.
  * `chain` This label identify which chain the job is from. It is the value of name field of multichain configuration, or `default` if multichain is not enabled.
  * `status` The session status, could be of the following values
    * `connected` The client is connected but no valid message exchanges are done.
    * `subscribed` The client has subscribed to the mining jobs.
    * `registering` The auto-registration is in-progress for the client.
    * `authenticated` The client is authenticated. Most of working clients shall be of this status.
* `sserver_idle_since_last_job_broadcast_seconds` The idle seconds since sserver broadcast the last job. If this metric is too higher (higher than several block time), it is likely that there is something wrong between jobmaker and sserver (or something wrong with jobmaker).
  * `chain` This label identify which chain the job is from. It is the value of name field of multichain configuration, or `default` if multichain is not enabled.
* `sserver_last_job_broadcast_height` The block height of the last broadcast job. If this metric stays at a value for a long time, it is likely that node synchronization may have some problems. If it goes down, it is likely that the pool has mined on a fork.
  * `chain` This label identify which chain the job is from. It is the value of name field of multichain configuration, or `default` if multichain is not enabled.
* `sserver_shares_per_second_since_last_scrape` Shares submitted per second since last scrape. This essentially represents the sserver load, but the factor needs to be measured case by case.
  * `chain` This label identify which chain the job is from. It is the value of name field of multichain configuration, or `default` if multichain is not enabled.
  * `status` The status of the share submitted.
    * `share_accepted` Share is accepted. Majority of the shares shall be of this state.
    * `share_accepted_stale` Share is stale but still accepted. This only applies to ETH/ETC as there are uncle block mechanism.
    * `share_accepted_and_solved` Share is accepted and is a valid block.
    * `share_accepted_and_solved_stale` Share is stale but still accepted and is a valid block. This only applies to ETH/ETC as there are uncle block mechanism.
    * `share_rejected` Share is rejected for an unknown reason.
    * `job_not_found_stale` Share is stale (submitted too late).
    * `duplicate_share` Share is duplicated.
    * `low_difficulty` Share difficulty is lower than job requirements.
    * `unauthorized_worker` Session is not authenticated.
    * `illegal_params` Share submission is malformatted.
    * `time_too_old` The block time in share submission is too old. This only applies to BTC and its forks.
    * `time_too_new` The block time in share submission is too new. This only applies to BTC and its forks.
    * `invalid_version_mask` The version mask in share submission is invalid. This only applies to BTC and its forks.
    * `invalid_solution` Share submission contains an invalid solution, This applies to coins with different solution and difficulty calculations.

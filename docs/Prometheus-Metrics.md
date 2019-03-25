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
  port = 8080
  # path of the prometheus exporter url
  path = "/metrics"
};
```

## Component Metrics

### ssever

Component sserver provides the following metrics

* `sserver_identity` This is the numeric identity of sserver. For servers of the same coin, this identity shall be uniquely assigned (we recommend to use the automatic assignment via ZooKeeper).
* `sserver_sessions_total` The total session count of sserver.
* `sserver_idle_since_last_job_broadcast_seconds` The idle seconds since sserver broadcast the last job. If this metric is too higher (higher than several block time), it is likely that there is something wrong between jobmaker and sserver (or something wrong with jobmaker).
  * `chain` This label identify which chain the job is from. It is the value of name field of multichain configuration, or `default` if multichain is not enabled.
* `sserver_last_job_broadcast_height` The block height of the last broadcast job. If this metric stays at a value for a long time, it is likely that node synchronization may have some problems. If it goes down, it is likely that the pool has mined on a fork.
  * `chain` This label identify which chain the job is from. It is the value of name field of multichain configuration, or `default` if multichain is not enabled.
* `sserver_shares_per_second_since_last_scrape` Shares submitted per second since last scrape. This essentially represents the sserver load, but the factor needs to be measured case by case.
  * `chain` This label identify which chain the job is from. It is the value of name field of multichain configuration, or `default` if multichain is not enabled.
  * `status` The status of the share submitted.
    * `Share accepted`
    * `Share accepted (stale)`
    * `Share accepted and solved`
    * `Share accepted and solved (stale)`
    * `Share rejected`
    * `Job not found (=stale)`
    * `Duplicate share`
    * `Low difficulty`
    * `Unauthorized worker`
    * `Illegal params`
    * `Time too old`
    * `Time too new`
    * `Invalid version mask`
    * `Invalid Solution`
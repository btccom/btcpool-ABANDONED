UPDATE shares INNER JOIN nearblocks ON nearblocks.currtime <= shares.timestamp AND shares.timestamp < nearblocks.nexttime SET shares.height=nearblocks.height WHERE shares.height = 0;
UPDATE shares INNER JOIN blocks ON shares.height = blocks.height SET shares.network_diff=blocks.diff WHERE shares.network_diff=0;

TABLE `poolwatcherreceivelog` (
  `poolname` varchar(25) NOT NULL,
  `poolhost` varchar(25) NOT NULL,
  `blockhash` varchar(64) NOT NULL,
  `blockheight` int(11) NOT NULL,
  `receivetime` datetime(3) NOT NULL,
  PRIMARY KEY (`poolname`,`blockhash`)
) ENGINE=InnoDB DEFAULT CHARSET=latin1;
CREATE TABLE `filedata` (
  `id` tinytext NOT NULL,
  `data` longblob NOT NULL,
  `datetime` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP
) ENGINE=InnoDB DEFAULT CHARSET=latin1;

package main

import (
	"flag"
	"os"

	"github.com/golang/glog"
	"github.com/urfave/cli"

	"github.com/btccom/btcpool/golang"
	"github.com/btccom/btcpool/golang/nodebridge/dcr"
	"github.com/btccom/btcpool/golang/nodebridge/eth"
)

func main() {
	// TODO: Make glog flags working
	_ = flag.CommandLine.Parse([]string{"-stderrthreshold=INFO"})

	app := cli.NewApp()
	app.Name = "nodebridge"
	app.Usage = "Bridges between the mining pool and the blockchain node"
	app.Version = golang.Version
	app.Commands = []cli.Command{
		dcr.Command,
		eth.Command,
	}

	err := app.Run(os.Args)
	if err != nil {
		glog.Exit(err)
	}
}

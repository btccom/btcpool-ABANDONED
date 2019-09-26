#!/usr/bin/env php
<?php
require_once __DIR__.'/functions.php';

define('API_KEY', '12345678');
define('JOBS_FILE', __DIR__.'/testdata/jobs-1.txt');
define('SOLUTIONS_FILE', __DIR__.'/testdata/solutions-1.txt');

class StratumListenerConnection extends ListenerConnection {
    protected $jobs = [];
    protected $solutions = [];
    protected $sentJob = [];
    protected $currJobId = 0;

    public function init() {
        $jobs = explode("\n", file_get_contents(JOBS_FILE));
        $solutions = explode("\n", file_get_contents(SOLUTIONS_FILE));

        foreach ($jobs as $json) {
            $job = json_decode(trim($json), true);
            if (is_array($job)) {
                $this->jobs[] = $job;
            }
        }

        foreach ($solutions as $json) {
            $sol = json_decode(trim($json), true);
            if (is_array($sol)) {
                $this->solutions[$sol['id']] = $sol;
            }
        }
    }

	public function readCallback($bev, $ctx) {
        while (($line = $bev->input->readLine(EventBuffer::EOL_LF)) != NULL) {
            $line = trim($line);

            echo "[recv] $line\n";

            if ($line == "STOP") {
                $this->base->stop();
                break;
            }

            $req = json_decode($line, true);
            if (!is_array($req)) {
                $this->sendResponse(-400, "Wrong JSON", "0");
                continue;
            }

            if (!isset($req['method'])) {
                $this->sendResponse(-400, "Missing method", "0");
                continue;
            }

            if (!isset($req['id'])) {
                $this->sendResponse(-400, "Missing id", "0");
                continue;
            }

            if (!is_string($req['id'])) {
                $this->sendResponse(-400, "id should be a string", "0");
                continue;
            }

            $method = $req['method'];
            $id = $req['id'];

            switch ($method) {
            case 'login':
                $this->login($req);
                break;
            case 'solution':
                $this->solution($req);
                break;
            default:
                $this->sendResponse(-400, "Unknown method", $id);
                break;
            }
        }
    }

    protected function login($req) {
        $id = $req['id'];

        if (!isset($req['api_key'])) {
            $this->sendResponse(-400, "Missing field 'api_key'", $id);
            return;
        }

        if (!is_string($req['api_key'])) {
            $this->sendResponse(-400, "api_key should be a string", $id);
            return;
        }

        if ($req['api_key'] !== API_KEY) {
            $this->sendResponse(-400, "Wrong api_key", $id);
            return;
        }

        $this->sendResponse(0, "Login successful", $id);
        $this->sendJob();
    }

    protected function sendJob() {
        if ($this->currJobId >= count($this->jobs)) {
            return;
        }

        $job = $this->jobs[$this->currJobId];
        $json = json_encode($job)."\n";

        echo "[job] $json";
        $this->bev->output->add($json);
        
        $this->sentJobs[$job['id']] = $job;
        $this->currJobId++;
    }

    protected function solution($req) {
        $id = $req['id'];

        if (!isset($this->sentJobs[$id])) {
            $this->sendResponse(-404, "Unknown job id", $id);
            return;
        }

        if (isset($this->sentJobs[$id]['stale'])) {
            $this->sendResponse(-405, "stale share", $id);
            return;
        }

        if (!isset($req['nonce'])) {
            $this->sendResponse(-400, "missing field 'nonce'", $id);
            return;
        }

        if (!isset($req['output'])) {
            $this->sendResponse(-400, "missing field 'output'", $id);
            return;
        }

        if (!isset($this->solutions[$id])) {
            $this->sendResponse(-505, "Unexpected solution", $id);
            return;
        }

        $sol = $this->solutions[$id];

        if ($req['nonce'] !== $sol['nonce']) {
            $this->sendResponse(-506, "Unexpected nonce", $id);
            return;
        }
        
        if ($req['output'] !== $sol['output']) {
            $this->sendResponse(-507, "Unexpected output", $id);
            return;
        }

        $this->sentJobs[$id]['stale'] = true;

        $this->sendResponse(1, "accepted", $id);
        $this->sendJob();
    }
    
    protected function sendResponse($code, $desc, $id) {
        $json = json_encode([
            "code" => "$code",
            "description" => "$desc",
            "id" => "$id",
            "jsonrpc" => "2.0",
            "method" => "result",
        ])."\n";

        $tag = ($code < 0) ? "error" : "send";
        echo "[$tag] $json";

        $this->bev->output->add($json);
    }
}

class StratumListener extends TCPListener {
    public function createListenerConnection($base, $fd) {
        return new StratumListenerConnection($base, $fd);
    }
}

$port = 9808;

if ($argc > 1) {
	$port = (int) $argv[1];
}
if ($port <= 0 || $port > 65535) {
	exit("Invalid port");
}

$l = new StratumListener($port);
echo "server running (listen on port $port)...\n";
$l->dispatch();
echo "server stopped.\n";

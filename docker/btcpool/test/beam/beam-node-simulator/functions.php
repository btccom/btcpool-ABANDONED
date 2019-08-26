<?php
// PHP library for TCP server

/*
 * Simple echo server based on libevent's connection listener.
 *
 * From: https://bitbucket.org/osmanov/pecl-event/src/master/examples/listener.php
 */

class ListenerConnection {
	protected $bev, $base;

	public function __destruct() {
		$this->bev->free();
	}

	public function __construct($base, $fd) {
		$this->base = $base;

		$this->bev = new EventBufferEvent($base, $fd, EventBufferEvent::OPT_CLOSE_ON_FREE);

		$this->bev->setCallbacks(array($this, "readCallback"), NULL,
			array($this, "eventCallback"), NULL);

		if (!$this->bev->enable(Event::READ)) {
			echo "Failed to enable READ\n";
			return;
		}

		$this->init();
	}

	protected function init() {
		// placeholder for child class
	}

	public function readCallback($bev, $ctx) {
		// Copy all the data from the input buffer to the output buffer
		
		// Variant #1
		$bev->output->addBuffer($bev->input);

		/* Variant #2 */
		/*
		$input	= $bev->getInput();
		$output = $bev->getOutput();
		$output->addBuffer($input);
		*/
	}

	public function eventCallback($bev, $events, $ctx) {
		if ($events & EventBufferEvent::ERROR) {
			echo "Error from bufferevent\n";
		}

		if ($events & (EventBufferEvent::EOF | EventBufferEvent::ERROR)) {
			//$bev->free();
			$this->__destruct();
		}
	}
}

class TCPListener {
	public $base,
		$listener,
		$socket;
	protected $conn = array();

	public function __destruct() {
		foreach ($this->conn as &$c) $c = NULL;
	}

	public function __construct($port) {
		$this->base = new EventBase();
		if (!$this->base) {
			echo "Couldn't open event base";
			exit(1);
		}

		// Variant #1
		/*
		$this->socket = socket_create(AF_INET, SOCK_STREAM, SOL_TCP);
		if (!socket_bind($this->socket, '0.0.0.0', $port)) {
			echo "Unable to bind socket\n";
			exit(1);
		}
		$this->listener = new EventListener($this->base,
			array($this, "acceptConnCallback"), $this->base,
			EventListener::OPT_CLOSE_ON_FREE | EventListener::OPT_REUSEABLE,
			-1, $this->socket);
		 */

		// Variant #2
 		$this->listener = new EventListener($this->base,
 			array($this, "acceptConnCallback"), $this->base,
 			EventListener::OPT_CLOSE_ON_FREE | EventListener::OPT_REUSEABLE, -1,
 			"0.0.0.0:$port");

		if (!$this->listener) {
			echo "Couldn't create listener";
			exit(1);
		}

		$this->listener->setErrorCallback(array($this, "accept_error_cb"));
	}

	public function dispatch() {
		$this->base->dispatch();
	}

	// This callback is invoked when there is data to read on $bev
	public function acceptConnCallback($listener, $fd, $address, $ctx) {
		// We got a new connection! Set up a bufferevent for it. */
		$base = $this->base;
		$this->conn[] = $this->createListenerConnection($base, $fd);
    }
    
    public function createListenerConnection($base, $fd) {
        return new ListenerConnection($base, $fd);
    }

	public function accept_error_cb($listener, $ctx) {
		$base = $this->base;

		fprintf(STDERR, "Got an error %d (%s) on the listener. "
			."Shutting down.\n",
			EventUtil::getLastSocketErrno(),
			EventUtil::getLastSocketError());

		$base->exit(NULL);
	}
}

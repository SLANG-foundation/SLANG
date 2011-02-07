<?php

/**
 * Configuration for a SLA-NG node.
 */
class SlangConfig {

	var $_node_id = null;
	var $_xml = null;

	// configuration variables with sane default values.
	var $_timestamp_type = "kernel";
	var $_debug = false;
	var $_interface = "eth2";
	var $_port = 60666;
	var $_timeout = 10;
	var $_dbpath = ":memory:";
	var $_fifopath = "/tmp/probed.fifo";
	var $_sessions;

	/**
	 * Constructor.
	 * @param integer $node_id ID of the node.
	 */
	function __construct($node_id) {

		$this->_node_id = $node_id;
		$this->probes = array();
		$this->_xml = simplexml_load_string("<config></config>");

	}

	/**
	 * Returns configuration as XML file.
	 * @return XML config as a string.
	 */
	function getAsXml() {

		// add children to config-element
		$this->_xml->addChild('timestamp', $this->_timestamp_type);
		$this->_xml->addChild('debug', intval($this->_debug));
		$this->_xml->addChild('interface', $this->_interface);
		$this->_xml->addChild('port', $this->_port);
		$this->_xml->addChild('timeout', $this->_timeout);
		$this->_xml->addChild('dbpath', $this->_dbpath);
		$this->_xml->addChild('fifopath', $this->_fifopath);
		
		// add probes
		foreach ($this->_sessions as $session) {
			$xmlSession = $this->_xml->addChild('probe');
			$xmlSession->addAttribute('id', $session['id']);
			$xmlSession->addChild('dscp', $session['dscp']);
			$xmlSession->addChild('interval', $session['interval']);
			$xmlSession->addChild('type', $session['type']);
		}

        return $this->_xml->asXML();
		
	}

	/**
	 * Add a measurement session
	 *
	 * Session data should be passed as an associative array with 
	 * the following fields:
	 * id: session ID
	 * dscp: DSCP
	 * interval: Probe interval in mucroseconds
	 * type: Session type
	 *
	 * @param array $session Session data.
	 */
	function addSession($session) {

		$this->_sessions[] = $session;

	}

}

?>


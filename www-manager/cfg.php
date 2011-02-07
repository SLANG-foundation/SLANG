<?php

	require_once("SlangConfig.class.php");

	// devices in our full mesh
	$fullMeshMembers = array('lab-slang-1.swip.net', 'lab-slang-2.swip.net', 'lab-slang-3.tele2.net', 'lab-slang-4.tele2.net', 'lab-slang-5.tele2.net');

	// create list of devices with measurement sessions
    // TODO: make the number of sessions per host more balanced
    $mesh = array();
    $sessid = 1;
	foreach ($fullMeshMembers as $dst) {

        $mesh[$dst] = array();
        
        foreach ($fullMeshMembers as $session) {
            if (!array_key_exists($session, $mesh)) {
                $mesh[$dst][$sessid] = $session;
                $sessid++;
            }
        }

    }

    print_r($mesh);

    $host = gethostbyaddr($_SERVER['REMOTE_ADDR']);
    if (!array_key_exists($host, $mesh)) {
        die("Unknown host.");
    }
	
   	$c = new SlangConfig($host);

    foreach ($mesh[$host] as $id => $dst) {
   
   		$data = array(
   			'id' => $id, 
   			'address' => $dst, 
   			'interval' => '100000',
   			'dscp' => 0, 
   			'type' => 'slang'
   		);

   		$c->addSession($data);
   
   	}

	print($c->getAsXml());

?>

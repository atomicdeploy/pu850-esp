void initNetDiscovery()
{
	// join mcast group
	if ( !discovery.begin(mcastIP, MCAST_PORT) ) {
		// error
	}
}

void loop_peer()
{
	ND_Packet localPacket, remotePacket;
	bool      announced = false;
	uint8_t   pktType;
	IPAddress localIP = WiFi.localIP();

	while ( true ) {
		if ( !announced ) {
			localPacket.payload[0] = localIP[3];      // each peer has a different ID
			discovery.announce(&localPacket);
		}
		pktType = discovery.listen(&remotePacket);
		switch ( pktType ) {
		case ND_ACK:
			// we have been acknowledged by a peer
			DEBUG_MSG(1, F("ACK"), F("received"));
			if ( announced ) {
				DEBUG_MSG(1, F("ACK"), F("ignored"));
			} else if ( remotePacket.payload[0] == localIP[3] ) {            // this is our ACK
				Serial.print(F("Discovered device at "));
				Serial.println((IPAddress)remotePacket.addressIP);
				Serial.print(F("Remote MAC: "));
				for ( int i = 0; i < WL_MAC_ADDR_LENGTH; i++ ) {
					Serial.print(remotePacket.addressMAC[i], HEX);
					if ( i < WL_MAC_ADDR_LENGTH-1 ) {
						Serial.print(".");
					}
				}
				announced = true;
				Serial.println();
			}
			break;

		case ND_ANNOUNCE:
			DEBUG_MSG(1, F("ANNOUNCE"), F("received"));
			localPacket.payload[0] = remotePacket.payload[0];                // retain the peer ID of the sender
			if ( discovery.ack(&localPacket) ) {
				DEBUG_MSG(1, F("ACK"), F("sent"));
			}
			break;

		case 0:
		default:
			break;
		}
		yield();
		delay(1000);
	}
}

void loop_rcv()
{
	ND_Packet localPacket, remotePacket;
	std::array<bool, 256> senderACK;

	senderACK.fill(false);
	while ( true ) {
		// listen for announcement packets & ACK it
		if ( discovery.listen(&remotePacket) == ND_ANNOUNCE ) {
			uint8_t senderID = remotePacket.payload[0];           // ID is the last octet in the IP address for unique identification

			if ( !senderACK[senderID] ) {
				// we have not yet acknowledged this sender
				localPacket.payload[0] = senderID;                 // return sender's ID
				if ( discovery.ack(&localPacket) ) {
					Serial.print(F("Discovered device at "));
					Serial.println((IPAddress)remotePacket.addressIP);
					Serial.print(F("Remote MAC: "));
					for ( int i = 0; i < WL_MAC_ADDR_LENGTH; i++ ) {
						Serial.print(remotePacket.addressMAC[i], HEX);
						if ( i < WL_MAC_ADDR_LENGTH - 1 ) {
							Serial.print(".");
						}
					}
					senderACK[senderID] = true;
					Serial.println();
				}
			}

		}
		yield();
		delay(1000);
	}
}

void loop_send()
{
	ND_Packet localPacket, remotePacket;
	IPAddress localIP = WiFi.localIP();
	bool      announced = false;

	localPacket.payload[0] = localIP[3];                 // unique peer ID in the case of multipe peers
	while ( !announced ) {
		// announce our presence - this may need to happen multiple times until the receiver acknowledges us
		if ( discovery.announce(&localPacket) ) {
			if ( discovery.listen(&remotePacket) == ND_ACK ) {
				DEBUG_MSG(1, "ACK", "received");
				if ( remotePacket.payload[0] == localIP[3] ) {    // is this ACK for us?
					Serial.print(F("Discovered device at "));
					Serial.println((IPAddress)remotePacket.addressIP);
					Serial.print(F("Remote MAC: "));
					for ( int i = 0; i < 6; i++ ) {
						Serial.print(remotePacket.addressMAC[i], HEX);
						if ( i < 5 ) {
							Serial.print(".");
						}
					}
					announced = true;
					discovery.stop();
					Serial.println();
				}
			}
		}
		yield();
		delay(1000);
	}
	while ( true ) delay(1000);
}
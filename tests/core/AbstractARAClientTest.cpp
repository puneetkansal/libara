/******************************************************************************
 Copyright 2012, The DES-SERT Team, Freie Universität Berlin (FUB).
 All rights reserved.

 These sources were originally developed by Friedrich Große
 at Freie Universität Berlin (http://www.fu-berlin.de/),
 Computer Systems and Telematics / Distributed, Embedded Systems (DES) group
 (http://cst.mi.fu-berlin.de/, http://www.des-testbed.net/)
 ------------------------------------------------------------------------------
 This program is free software: you can redistribute it and/or modify it under
 the terms of the GNU General Public License as published by the Free Software
 Foundation, either version 3 of the License, or (at your option) any later
 version.

 This program is distributed in the hope that it will be useful, but WITHOUT
 ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

 You should have received a copy of the GNU General Public License along with
 this program. If not, see http://www.gnu.org/licenses/ .
 ------------------------------------------------------------------------------
 For further information and questions please use the web site
 http://www.des-testbed.net/
 *******************************************************************************/

#include "CppUTest/TestHarness.h"
#include "PacketTrap.h"
#include "RoutingTable.h"
#include "Packet.h"
#include "PacketType.h"
#include "Address.h"
#include "LinkedList.h"
#include "Pair.h"

#include "testAPI/mocks/ARAClientMock.h"
#include "testAPI/mocks/PacketMock.h"
#include "testAPI/mocks/NetworkInterfaceMock.h"
#include "testAPI/mocks/AddressMock.h"

using namespace ARA;

TEST_GROUP(AbstractARAClientTest) {};

TEST(AbstractARAClientTest, testPacketGetsTrappedIfNotDeliverable) {
    ARAClientMock client = ARAClientMock();
    PacketTrap* packetTrap = client.getPacketTrap();
    RoutingTable* routingTable = client.getRoutingTable();
    PacketMock packet = PacketMock();

    CHECK(routingTable->isDeliverable(&packet) == false);
    client.sendPacket(&packet);
    CHECK(packetTrap->contains(&packet));
}

TEST(AbstractARAClientTest, testGeneralBroadCast) {
    ARAClientMock client = ARAClientMock();
    NetworkInterfaceMock* interface1 = client.getNewNetworkInterfaceMock();
    NetworkInterfaceMock* interface2 = client.getNewNetworkInterfaceMock();
    NetworkInterfaceMock* interface3 = client.getNewNetworkInterfaceMock();

    PacketMock packet = PacketMock();
    client.broadCast(&packet);

    CHECK(interface1->hasPacketBeenBroadCasted(&packet) == true);
    CHECK(interface2->hasPacketBeenBroadCasted(&packet) == true);
    CHECK(interface3->hasPacketBeenBroadCasted(&packet) == true);
}

TEST(AbstractARAClientTest, testGetNextSequenceNumber) {
    ARAClientMock client = ARAClientMock();
    for (unsigned int i = 1; i < 100; i++) {
        CHECK_EQUAL(i, client.getNextSequenceNumber());
    }
}

TEST(AbstractARAClientTest, testBroadcastFANTIfPacketNotDeliverable) {
    ARAClientMock client = ARAClientMock();
    RoutingTable* routingTable = client.getRoutingTable();
    NetworkInterfaceMock* interface = client.getNewNetworkInterfaceMock();
    PacketMock packet = PacketMock();

    CHECK(routingTable->isDeliverable(&packet) == false);
    client.sendPacket(&packet);

    LONGS_EQUAL(1, interface->getNumberOfSentPackets());
    Pair<Packet, Address>* sentPacketInfo = interface->getSentPackets()->getFirst();
    Packet* sentPacket = sentPacketInfo->getLeft();
    Address* recipientOfSentPacket = sentPacketInfo->getRight();

    CHECK(sentPacket->getType() == PacketType::FANT);
    CHECK(recipientOfSentPacket->isBroadCast());
}

TEST(AbstractARAClientTest, testSendPacketToNextHopIfRouteIsKnown) {
    ARAClientMock client = ARAClientMock();
    RoutingTable* routingTable = client.getRoutingTable();
    NetworkInterfaceMock* interface1 = client.getNewNetworkInterfaceMock();
    NetworkInterfaceMock* interface2 = client.getNewNetworkInterfaceMock();
    NetworkInterfaceMock* interface3 = client.getNewNetworkInterfaceMock();
    PacketMock originalPacket = PacketMock();
    AddressMock nextHop = AddressMock("nextHop");

    // make sure that a route to the packet destination is available
    routingTable->update(originalPacket.getDestination(), &nextHop, interface2, 1.0);
    CHECK(routingTable->isDeliverable(&originalPacket) == true);

    client.sendPacket(&originalPacket);

    // check if packet has been send over the correct interface
    CHECK(interface1->hasPacketBeenSend(&originalPacket) == false);
    CHECK(interface2->hasPacketBeenSend(&originalPacket) == true);
    CHECK(interface3->hasPacketBeenSend(&originalPacket) == false);

    // check if packet has been send via interface2 to nextHop
    Pair<Packet, Address>* sentPacketInfo = interface2->getSentPackets()->getFirst();
    Packet* sentPacket = sentPacketInfo->getLeft();
    Address* recipientOfSentPacket = sentPacketInfo->getRight();
    CHECK(recipientOfSentPacket->equals(&nextHop));

    // Check that packet content is basically the same
    CHECK_EQUAL(originalPacket.getType(), sentPacket->getType());
    CHECK(sentPacket->getSource()->equals(originalPacket.getSource()));
    CHECK(sentPacket->getDestination()->equals(originalPacket.getDestination()));
    CHECK_EQUAL(originalPacket.getSequenceNumber(), sentPacket->getSequenceNumber());
    CHECK_EQUAL(originalPacket.getPayload(), sentPacket->getPayload());
    CHECK_EQUAL(originalPacket.getPayloadLength(), sentPacket->getPayloadLength());

    // only the hop count needs to be incremented by 1
    CHECK_EQUAL(originalPacket.getHopCount() + 1, sentPacket->getHopCount());
}

TEST(AbstractARAClientTest, testGetNumberOfNetworkInterfaces) {
    ARAClientMock client = ARAClientMock();
    NetworkInterfaceMock interface1 = NetworkInterfaceMock();
    NetworkInterfaceMock interface2 = NetworkInterfaceMock();
    NetworkInterfaceMock interface3 = NetworkInterfaceMock();
    CHECK_EQUAL(0, client.getNumberOfNetworkInterfaces());

    client.addNetworkInterface(&interface1);
    CHECK_EQUAL(1, client.getNumberOfNetworkInterfaces());

    client.addNetworkInterface(&interface2);
    CHECK_EQUAL(2, client.getNumberOfNetworkInterfaces());

    client.addNetworkInterface(&interface3);
    CHECK_EQUAL(3, client.getNumberOfNetworkInterfaces());
}

TEST(AbstractARAClientTest, testGetNetworkInterface) {
    ARAClientMock client = ARAClientMock();
    NetworkInterfaceMock interface1 = NetworkInterfaceMock();
    NetworkInterfaceMock interface2 = NetworkInterfaceMock();
    NetworkInterfaceMock interface3 = NetworkInterfaceMock();
    client.addNetworkInterface(&interface1);
    client.addNetworkInterface(&interface2);
    client.addNetworkInterface(&interface3);

    CHECK(client.getNetworkInterface(0) == &interface1);
    CHECK(client.getNetworkInterface(1) == &interface2);
    CHECK(client.getNetworkInterface(2) == &interface3);
}

TEST(AbstractARAClientTest, testRegisterReceivedPacket) {
    ARAClientMock client = ARAClientMock();
    PacketMock packet1 = PacketMock("A", "B", 123);
    PacketMock packet2 = PacketMock("A", "B", 124);  // same source but different seqNr
    PacketMock packet3 = PacketMock("C", "A", 123);  // same seqNr but different source
    PacketMock packet4 = PacketMock("A", "C", 123);  // the source and seqNr are equal to packet1

    CHECK_FALSE(client.hasBeenReceivedEarlier(&packet1));
    CHECK_FALSE(client.hasBeenReceivedEarlier(&packet2));
    CHECK_FALSE(client.hasBeenReceivedEarlier(&packet3));
    CHECK_FALSE(client.hasBeenReceivedEarlier(&packet4));

    client.registerReceivedPacket(&packet1);
    CHECK_TRUE(client.hasBeenReceivedEarlier(&packet1));
    CHECK_FALSE(client.hasBeenReceivedEarlier(&packet2));
    CHECK_FALSE(client.hasBeenReceivedEarlier(&packet3));
    CHECK_TRUE(client.hasBeenReceivedEarlier(&packet4));
}

/**
 * In this test we simulate that the same packet has been received
 * twice at node x. The packet is directed from node A to node B and
 * has been relayed via node C to node X.
 * Node x must respond to node C with a DUPLICATE_WARNING packet.
 */
/* FIXME make this test work next (Friedrich)
TEST(AbstractARAClientTest, testRememberLastRecievedPackets) {
    // prepare a packet
    ARAClientMock client = ARAClientMock();
    PacketMock packet = PacketMock("A", "B", 123);
    AddressMock nodeC = AddressMock("C");
    AddressMock localNode = AddressMock("X");
    //packet.setSender(&nodeC);

    // let client receive the packet over the same interface twice
    NetworkInterfaceMock* interface = client.getNewNetworkInterfaceMock("X");
    client.receivePacket(&packet, interface);
    client.receivePacket(&packet, interface);

    // the client should now have sent a duplicate warning back over the interface
    LONGS_EQUAL(1, interface->getNumberOfSentPackets());

    Pair<Packet, Address>* sentPacketInfo = interface->getSentPackets()->getFirst();
    Packet* sentPacket = sentPacketInfo->getLeft();
    Address* recipientOfSentPacket = sentPacketInfo->getRight();

    // check the contents of the duplicate warning packet
    CHECK(recipientOfSentPacket->equals(&nodeC));
    CHECK(sentPacket->getSender()->equals(&localNode));
    CHECK(sentPacket->getSource()->equals(&localNode));
    CHECK(sentPacket->getType() == PacketType::DUPLICATE_WARNING);
    CHECK_EQUAL(1, sentPacket->getHopCount());
    CHECK_EQUAL(0, sentPacket->getPayloadLength());
}*/

/*
 * $FU-Copyright$
 */

#include "TestbedARAClient.h"


TESTBED_NAMESPACE_BEGIN

TestbedARAClient::TestbedARAClient(Configuration& configuration) : AbstractARAClient(configuration) {
    // set the clock to the standard clock (if it is not pre-set to the dummy clock, the tests fail)
    Environment::setClock(new StandardClock());
    //TODO Make configurable
    Logger* logger = new SimpleLogger("ara");
    setLogger(logger);
    // DEBUG: logDebug("Initialized testbedARAClient");
    initializeNetworkInterfaces();
    // DEBUG: logDebug("Initialized testbedARAClient network Interfaces");
}

std::string TestbedARAClient::toString() { 
    std::ostringstream result;
    result << std::endl;

    result << "initial pheromone value:                     " << initialPheromoneValue << std::endl;
    result << "maximum time to live:                        " << packetFactory->getMaximumNrOfHops() << std::endl;
//    result << "evporation policy:                           " << evaporationPolicy->toString() << std::endl;

    result << "max number of route discovery retries:       " << maxNrOfRouteDiscoveryRetries << std::endl;
    result << "packet delivery delay [ms]:                  " << packetDeliveryDelayInMilliSeconds << std::endl;
    result << "route discovery timeout [ms]:                " << routeDiscoveryTimeoutInMilliSeconds << std::endl;
    result << "neighbor activity check interval [ms]:       " << neighborActivityCheckIntervalInMilliSeconds << std::endl;
    result << "max neighbor inactivity check interval [ms]: " << maxNeighborInactivityTimeInMilliSeconds << std::endl;
    result << "pant interval [ms]:                          " << pantIntervalInMilliSeconds << std::endl;

    return result.str();
}

std::string TestbedARAClient::getStatistics() { 
    std::ostringstream result;
   
    // print network interface statistics
    for (auto& interface: interfaces) {
        TestbedNetworkInterface* testbedInterface = dynamic_cast<TestbedNetworkInterface*>(interface);

        if (testbedInterface) {
            result << "Network Interface Statistics [" << testbedInterface->getInterfaceName() << "]" << std::endl;
            result << testbedInterface->getStatistics();
        }
    }

    result << AbstractARAClient::getStatistics();
   
    return result.str();
}

void TestbedARAClient::sendPacket(Packet* packet) {
    // DEBUG: std::cerr << "[TestbedARAClient::sendPacket] pass packet to client" << std::endl;
    AbstractARAClient::sendPacket(packet);
}

void TestbedARAClient::receivePacket(Packet* packet, ARA::NetworkInterface* interface) {
    logDebug("receiving packet # %u type %s over interface at %s", packet->getSequenceNumber(), PacketType::getAsString(packet->getType()).c_str(), interface->getLocalAddress()->toString().c_str());
    AbstractARAClient::receivePacket(packet, interface);
    //TODO: persistRoutingTableData
}

void TestbedARAClient::deliverToSystem(const Packet* packet) {
    logDebug("attempting to send packet # %u to System via TAP", packet->getSequenceNumber());

    struct ether_header* payload;
    const TestbedPacket* testbedPacket = dynamic_cast<const TestbedPacket*>(packet);
    int payloadLength = dessert_msg_ethdecap(testbedPacket->toDessertMessage(), &payload); 

    if (payloadLength != -1) {
        /// send the payload to the system
        if (dessert_syssend(payload, payloadLength) != DESSERT_OK){
            logFatal("sending packet to system failed");
        }
        /// since the data was allocated using malloc indessert_msg_ethdecap()
        free(payload);
    }
}

void TestbedARAClient::packetNotDeliverable(const Packet* packet) {
    logDebug("packet # %u is undeliverable", packet->getSequenceNumber());
    delete packet;
}

void TestbedARAClient::initializeNetworkInterfaces() {
    dessert_meshif_t* dessertInterfaces = dessert_meshiflist_get();

    // the broadcast address of the interface (FIXME: might cause trouble)
    std::shared_ptr<TestbedAddress> broadcastAddress = std::make_shared<TestbedAddress>(DESSERT_BROADCAST_ADDRESS); 

    while(dessertInterfaces != nullptr) {
        // the name of the interface
        std::string interfaceName(dessertInterfaces->if_name);
        // the hardware address of the interface 
        std::shared_ptr<TestbedAddress> hardwareAddress = std::make_shared<TestbedAddress>(dessertInterfaces->hwaddr);

        TestbedNetworkInterface* newInterface = new TestbedNetworkInterface(interfaceName, this, hardwareAddress, broadcastAddress, packetFactory, 400000);

        addNetworkInterface(newInterface);
        logDebug("initialized network interface: %s", dessertInterfaces->if_name);
        dessertInterfaces = dessertInterfaces->next;
    }

    tapAddress = std::make_shared<TestbedAddress>(dessert_l25_defsrc);
    std::cerr << "[initializeNetworkInterfaces] tap address is: " << tapAddress->toString() << std::endl;
}

bool TestbedARAClient::isLocalAddress(AddressPtr address) const {
    return (address.get()->equals(tapAddress) || AbstractNetworkClient::isLocalAddress(address));
}

void TestbedARAClient::broadcastFANT(AddressPtr destination) {
    unsigned int sequenceNr = getNextSequenceNumber();

    for(auto& interface: interfaces) {
        Packet* fant = packetFactory->makeFANT(tapAddress, destination, sequenceNr);
        interface->broadcast(fant);
    }
}

std::string TestbedARAClient::routingTableToString() {
    return this->routingTable->toString();
}

void TestbedARAClient::handleExpiredRouteDiscoveryTimer(std::weak_ptr<Timer> routeDiscoveryTimer){
    std::lock_guard<std::recursive_mutex> routeDiscoveryTimerLock(routeDiscoveryTimerMutex);
    AbstractARAClient::handleExpiredRouteDiscoveryTimer(routeDiscoveryTimer);

    // should come up with our own handling
}

void TestbedARAClient::handleExpiredDeliveryTimer(std::weak_ptr<Timer> deliveryTimer){
    std::shared_ptr<Timer> timer = deliveryTimer.lock();

    if (timer) {
        TestbedTimerAddressInfo* timerInfo = (TestbedTimerAddressInfo*) timer->getContextObject();
        AddressPtr destination = timerInfo->getDestination();

        if (destination) {
            // DEBUG:
            std::cerr << "[TestbedARAClient::handleExpiredDeliveryTimer] trying to find destination " <<  destination->toString() << 
                " in running route discoveries " << std::endl;

            RunningRouteDiscoveriesMap::const_iterator discovery;
            std::lock_guard<std::recursive_mutex> routeDiscoveryTimerLock(routeDiscoveryTimerMutex);
            discovery = runningRouteDiscoveries.find(destination);

            if (discovery != runningRouteDiscoveries.end()) {
                /**
                 * We delete the route discovery info first or else the client will 
                 * always think the route discovery is still running and never send any packets
                 */
                runningRouteDiscoveries.erase(discovery);

                std::lock_guard<std::mutex> lock(deliveryTimerMutex);
                runningDeliveryTimers.erase(timer);

                delete timerInfo;
                sendDeliverablePackets(destination);
            } else {
                logError("Could not find running route discovery object for destination %s)", destination->toString().c_str());
            }
        }
    } else {
        // DEBUG: 
        std::cerr << "[TestbedARAClient::handleExpiredDeliveryTimer] shared_ptr expired " << std::endl;
    }

}

void TestbedARAClient::handleExpiredPANTTimer(std::weak_ptr<Timer> pantTimer){
    std::lock_guard<std::mutex> lock(pantTimerMutex);
    AbstractARAClient::handleExpiredPANTTimer(pantTimer);
}

void TestbedARAClient::startRouteDiscoveryTimer(const Packet* packet){
    std::cerr << "[TestbedARAClient::startRouteDiscoveryTimer] start route discovery timer" << std::endl; 

    TestbedRouteDiscoveryInfo* discoveryInfo = new TestbedRouteDiscoveryInfo(packet);
    TimerPtr timer = getNewTimer(TimerType::ROUTE_DISCOVERY_TIMER, discoveryInfo);
    timer->addTimeoutListener(this);
    timer->run(routeDiscoveryTimeoutInMilliSeconds * 1001);

    AddressPtr destination = packet->getDestination();

    std::lock_guard<std::recursive_mutex> lock(routeDiscoveryTimerMutex);
    runningRouteDiscoveries[destination] = timer;
}

void TestbedARAClient::stopRouteDiscoveryTimer(AddressPtr destination){
    std::lock_guard<std::recursive_mutex> lock(routeDiscoveryTimerMutex);
    AbstractARAClient::stopRouteDiscoveryTimer(destination);
}

bool TestbedARAClient::isRouteDiscoveryRunning(AddressPtr destination) {
    std::lock_guard<std::recursive_mutex> lock(routeDiscoveryTimerMutex);
    return AbstractARAClient::isRouteDiscoveryRunning(destination);
}

TestbedNetworkInterface* TestbedARAClient::getTestbedNetworkInterface(std::shared_ptr<TestbedAddress> address){
    std::lock_guard<std::mutex> lock(networkInterfaceMutex);
    
    if (interfaces.size() == 1) {
        return dynamic_cast<TestbedNetworkInterface*>(interfaces[0]);
    }

    /** 
     * if we really use at some point multiple interfaces, we have to improve
     * the management of the interfaces
     */
    if (interfaces.size() > 1) {
        for (unsigned int i = 0; i < interfaces.size(); i++) {
            if (interfaces[i]->getLocalAddress() == address) {
                return dynamic_cast<TestbedNetworkInterface*>(interfaces[i]);
            }
        }
    }

    return nullptr;
}

void TestbedARAClient::startDeliveryTimer(AddressPtr destination) {
    TestbedTimerAddressInfo* contextObject = new TestbedTimerAddressInfo(destination);
    TimerPtr timer = getNewTimer(TimerType::DELIVERY_TIMER, contextObject);
    timer->addTimeoutListener(this);
    timer->run(packetDeliveryDelayInMilliSeconds * 1000);

    std::lock_guard<std::mutex> lock(deliveryTimerMutex);
    runningDeliveryTimers.insert(timer);
}

TESTBED_NAMESPACE_END

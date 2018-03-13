//
//                           SimuLTE
//
// This file is part of a software released under the license included in file
// "license.pdf". This license can be also found at http://www.ltesimulator.com/
// The above file and the present reference are part of the software itself,
// and cannot be removed from it.
//

#include "corenetwork/binder/LteBinder.h"
#include "inet/networklayer/common/L3AddressResolver.h"
#include <cctype>

#include "../lteCellInfo/LteCellInfo.h"
#include "corenetwork/nodes/InternetMux.h"

using namespace std;

Define_Module(LteBinder);

void LteBinder::setTransportAppPort(cModule* module, unsigned int counter, cXMLAttributeMap attr)
{
    // obtain app type
    cXMLAttributeMap::iterator jt;
    jt = attr.find("type");
    std::string appType = jt->second;

    std::string basePort;
    int port;

    // the sender is located inside the UE
    if (appType == "VoIPSender")
    {
        // read the destination base port from xml
        basePort = attr.find("baseDestPort")->second;
        // Add an offset equal to the number of app of the same type that have been already configured
        port = atoi(basePort.c_str()) + counter;
        EV << "LteBinder::setTransportAppPort - setting dest port to " << port << endl;

        module->par("destPort") = port;
    }
    // the receiver is located inside the eNb
    else if (appType == "VoIPReceiver")
    {
        // read the local base port from xml.
        basePort = attr.find("baseLocalPort")->second;
        // Add an offset equal to the number of app of the same type that have been already configured
        port = atoi(basePort.c_str()) + counter;
        EV << "LteBinder::setTransportAppPort - setting Local port to " << port << endl;

        module->par("localPort") = port;
    }
}

void LteBinder::parseParam(cModule* module, cXMLAttributeMap attr)
{
    cXMLAttributeMap::iterator it;
    for (it = attr.begin(); it != attr.end(); ++it)
    {
        if (it->first == "num" || it->first == "type" || it->first == "baseLocalPort" || it->first == "baseDestPort")
            continue;
        // FIXME numbers could start with negative sign - or even space !!!
        if (isdigit((it->second)[0]))
        {
            if (it->second.find(".") != string::npos)
                module->par((it->first).c_str()) = atof((it->second).c_str());
            else
                module->par((it->first).c_str()) = atoi((it->second).c_str());
        }
        else
            module->par((it->first).c_str()) = it->second;
    }
}

cModule* LteBinder::createNodeB(EnbType type)
{
    cModule* lteInternet = getSimulation()->getModuleByPath("lteInternet");

    cModuleType *mt = cModuleType::get("lte.corenetwork.nodes.eNodeB");

    cDatarateChannel * iChOut = cDatarateChannel::create("internetChannelOut");
    cDatarateChannel * iChIn = cDatarateChannel::create("internetChannelIn");

    // setting infinite datarate for internet Channel
        iChOut->setDatarate(0.0);
        iChIn->setDatarate(0.0);

        cModule *enodeb = mt->create("enodeb", getSimulation()->getSystemModule());
        MacNodeId cellId = registerNode(enodeb, ENODEB);

        lteInternet->setGateSize("peerLteIp",
            (lteInternet->gateSize("peerLteIp") + 1));

        // connecting to lteInternet : creating multiple gates for multiple nodeBs

        enodeb->gate("peerLteIp$o")->connectTo(
            lteInternet->gate("peerLteIp$i",
                lteInternet->gateSize("peerLteIp$i") - 1), iChOut);
        lteInternet->gate("peerLteIp$o", lteInternet->gateSize("peerLteIp$o") - 1)->connectTo(
            enodeb->gate("peerLteIp$i"), iChIn);

        // access the internet multiplexer

        cModule *mux = lteInternet->getSubmodule("mux");

        // prepare new mux gates
        mux->setGateSize("inExt", (mux->gateSize("inExt") + 1));
        mux->setGateSize("outExt", (mux->gateSize("outExt") + 1));

        // create inner lteInternet queue and connect it to internal gates, then initialize the mux.

        cModuleType *it = cModuleType::get("lte.corenetwork.lteip.InternetQueue");
        cModule *iQueue = it->create("internetqueue", lteInternet);

        // fix all connections

        // LTE Internet ingress goes to MUX ingress.
        lteInternet->gate("peerLteIp$i", lteInternet->gateSize("peerLteIp$i") - 1)->connectTo(
            mux->gate("inExt", mux->gateSize("inExt") - 1));
        // MUX Output goes to queue input
        mux->gate("outExt", mux->gateSize("outExt") - 1)->connectTo(
            iQueue->gate("lteIpIn"));
        // queue output goes to lteInternet output
        iQueue->gate("internetChannelOut")->connectTo(
            lteInternet->gate("peerLteIp$o",
                lteInternet->gateSize("peerLteIp$o") - 1));

        // register mux routing entry
        (dynamic_cast<InternetMux*>(mux))->setRoutingEntry(cellId,
            mux->gate("outExt", mux->gateSize("outExt") - 1));

        // finalize building of new modules

        enodeb->finalizeParameters();
        enodeb->buildInside();
        enodeb->scheduleStart(simTime());

        iQueue->finalizeParameters();
        iQueue->buildInside();
        iQueue->scheduleStart(simTime());

        // initialize newly added internet queue
        iQueue->callInitialize();

        //initializeAllChannels(enodeb);

        iChOut->callInitialize();
        iChIn->callInitialize();

        EV << "LteBinder::createNodeB - enbType set to " << type << endl;
        return enodeb;
    }

void LteBinder::transportAppAttach(cModule* parentModule, cModule* appModule,
    std::string transport)
{
    // prepare transport gates strings
    string appTgateOut, appTgateIn;

    if (transport == "udp" || transport == "tcp")
    {
        appTgateOut = transport + "Out";
        appTgateIn = transport + "In";
    }
    else
        throw cRuntimeError("LteBinder::transportAppAttach(): unrecognized transport layer %s",
            transport.c_str());

    cModule* tLayer = parentModule->getSubmodule(transport.c_str());

    tLayer->setGateSize("appIn", (tLayer->gateSize("appIn") + 1));
    tLayer->setGateSize("appOut", (tLayer->gateSize("appOut") + 1));

    appModule->gate(appTgateOut.c_str())->connectTo(
        tLayer->gate("appIn", (tLayer->gateSize("appIn") - 1)));

    tLayer->gate("appOut", (tLayer->gateSize("appOut") - 1))->connectTo(
        appModule->gate(appTgateIn.c_str()));
}

void LteBinder::attachAppModule(cModule *parentModule, std::string IPAddr,
    cXMLAttributeMap attr, int counter)
{
    cXMLAttributeMap::iterator jt;
    jt = attr.find("type");
    std::string appType = jt->second;

    EV << NOW << " LteBinder::attachAppModule " << appType << " from "
       << parentModule->getName() << " to IP " << IPAddr.c_str() << endl;

    cModuleType *mt = NULL;
    cModule *module = NULL;

    if (appType == "UDPSink")
    {
        mt = cModuleType::get("inet.applications.udpapp.UDPSink");
        module = mt->create("udpApp", parentModule);
        transportAppAttach(parentModule, module, string("udp"));
    }
    else if (appType == "UDPBasicApp")
    {
        mt = cModuleType::get("inet.applications.udpapp.UDPBasicApp");
        module = mt->create("udpApp", parentModule);
        module->par("destAddresses") = IPAddr;
        transportAppAttach(parentModule, module, string("udp"));
    }
    else if (appType == "VoIPSender")
    {
        mt = cModuleType::get("voip.VoIPSender");
        module = mt->create("udpApp", parentModule);
        module->par("destAddress") = IPAddr;
        transportAppAttach(parentModule, module, string("udp"));
        if (counter != -1) // set the appropriate port for UL direction
            setTransportAppPort(module, counter, attr);
    }
    else if (appType == "VoIPReceiver")
    {
        mt = cModuleType::get("voip.VoIPReceiver");
        // use a different name foreach UL voip receiver (eg: udpApp1)
        std::stringstream str;
        str << "udpApp";
        if (counter != -1)    // only in UL
            str << counter;
        module = mt->create(str.str().c_str(), parentModule);
        transportAppAttach(parentModule, module, string("udp"));
        if (counter != -1) // set the appropriate port for UL direction
            setTransportAppPort(module, counter, attr);
    }
    else if (appType == "TCPBasicClientApp")
    {
        mt = cModuleType::get("inet.applications.tcpapp.TCPBasicClientApp");
        module = mt->create("tcpApp", parentModule);
        module->par("connectAddress") = IPAddr;
        transportAppAttach(parentModule, module, string("tcp"));
    }
    else if (appType == "TCPSinkApp")
    {
        mt = cModuleType::get("inet.applications.tcpapp.TCPSinkApp");
        module = mt->create("tcpApp", parentModule);
        transportAppAttach(parentModule, module, string("tcp"));
    }
    else if (appType == "VoDUDPServer")
    {
        mt = cModuleType::get("vod.VoDUDPServer");
        module = mt->create("vodServer", parentModule);
        module->par("destAddresses") = IPAddr;
        transportAppAttach(parentModule, module, string("udp"));
    }
    else if (appType == "VoDUDPClient")
    {
        mt = cModuleType::get("vod.VoDUDPClient");
        module = mt->create("vodClient", parentModule);
        transportAppAttach(parentModule, module, string("udp"));
    }
    else if (appType == "gaming")
    {
        mt = cModuleType::get("gaming.gaming");
        module = mt->create("gaming", parentModule);
        module->par("destAddresses") = IPAddr;
        transportAppAttach(parentModule, module, string("udp"));
    }
    else if (appType == "ftp_3gpp_sender")
    {
        mt = cModuleType::get("ftp_3gpp.ftp_3gpp_sender");
        module = mt->create("ftp_3gpp_sender", parentModule);
        module->par("destAddresses") = IPAddr;
        transportAppAttach(parentModule, module, string("udp"));
    }
    else if (appType == "ftp_3gpp_sink")
    {
        mt = cModuleType::get("ftp_3gpp.ftp_3gpp_sink");
        module = mt->create("udpApp", parentModule);
        transportAppAttach(parentModule, module, string("udp"));
    }
    else
    {
        throw cRuntimeError("LteBinder::attachAppModule(): unrecognized application type %s", appType.c_str());
    }

    parseParam(module, attr);
    module->finalizeParameters();
    module->buildInside();
    module->scheduleStart(simTime());

    // if we are attaching app to the Internet Module , we have to initialize the app , too
//    if (strcmp(parentModule->getName() ,"lteInternet")==0)
//    {
//        module->callInitialize();
//    }
}

void LteBinder::unregisterNode(MacNodeId id)
{
    EV << NOW << " LteBinder::unregisterNode - unregistering node " << id << endl;

    if(nodeIds_.erase(id) != 1){
        EV_ERROR << "Cannot unregister node - node id \"" << id << "\" - not found";
    }
    std::map<IPv4Address, MacNodeId>::iterator it;
    for(it = macNodeIdToIPAddress_.begin(); it != macNodeIdToIPAddress_.end(); )
    {
        if(it->second == id)
        {
            macNodeIdToIPAddress_.erase(it++);
        }
        else
        {
            it++;
        }
    }
}

MacNodeId LteBinder::registerNode(cModule *module, LteNodeType type,
    MacNodeId masterId)
{
    Enter_Method_Silent("registerNode");

    MacNodeId macNodeId = -1;

    if (type == UE)
    {
        macNodeId = macNodeIdCounter_[2]++;
    }
    else if (type == RELAY)
    {
        macNodeId = macNodeIdCounter_[1]++;
    }
    else if (type == ENODEB)
    {
        macNodeId = macNodeIdCounter_[0]++;
    }

    EV << "LteBinder : Assigning to module " << module->getName()
       << " with OmnetId: " << module->getId() << " and MacNodeId " << macNodeId
       << "\n";

    // registering new node to LteBinder

    nodeIds_[macNodeId] = module->getId();

    module->par("macNodeId") = macNodeId;

    if (type == RELAY || type == UE)
    {
        registerNextHop(masterId, macNodeId);
    }
    else if (type == ENODEB)
    {
        module->par("macCellId") = macNodeId;
        registerNextHop(macNodeId, macNodeId);
    }
    return macNodeId;
}

void LteBinder::registerNextHop(MacNodeId masterId, MacNodeId slaveId)
{
    Enter_Method_Silent("registerNextHop");
    EV << "LteBinder : Registering slave " << slaveId << " to master "
       << masterId << "\n";

    if (masterId != slaveId)
    {
        dMap_[masterId][slaveId] = true;
    }

    if (nextHop_.size() <= slaveId)
        nextHop_.resize(slaveId + 1);
    nextHop_[slaveId] = masterId;
}

void LteBinder::initialize(int stage)
{
    if (stage == inet::INITSTAGE_LOCAL)
    {
        numBands_ = par("numBands");

        const char * stringa;

        std::vector<int> apppriority;
        std::vector<double> appdelay;
        std::vector<double> applossrate;

        stringa = par("priority");
        apppriority = cStringTokenizer(stringa).asIntVector();
        stringa = par("packetDelayBudget");
        appdelay = cStringTokenizer(stringa).asDoubleVector();
        stringa = par("packetErrorLossRate");
        applossrate = cStringTokenizer(stringa).asDoubleVector();

        for (int i = 0; i < LTE_QCI_CLASSES; i++)
        {
            QCIParam_[i].priority = apppriority[i];
            QCIParam_[i].packetDelayBudget = appdelay[i];
            QCIParam_[i].packetErrorLossRate = applossrate[i];
        }
        nodesConfigured_ = false;

        // execute node creation and setup.
        // nodesConfiguration();
    }
}

std::string LteBinder::increment_address(const char* address_string)  //TODO unused function
{
    IPv4Address addr(address_string);
    return IPv4Address(addr.getInt() + 1).str();
}

int LteBinder::getQCIPriority(int QCI)
{
    return QCIParam_[QCI - 1].priority;
}

double LteBinder::getPacketDelayBudget(int QCI)
{
    return QCIParam_[QCI - 1].packetDelayBudget;
}

double LteBinder::getPacketErrorLossRate(int QCI)
{
    return QCIParam_[QCI - 1].packetErrorLossRate;
}

void LteBinder::unregisterNextHop(MacNodeId masterId, MacNodeId slaveId)
{
    Enter_Method_Silent("unregisterNextHop");
    EV << "LteBinder : Unregistering slave " << slaveId << " from master "
       << masterId << "\n";
    dMap_[masterId][slaveId] = false;

    if (nextHop_.size() <= slaveId)
        return;
    nextHop_[slaveId] = 0;
}

OmnetId LteBinder::getOmnetId(MacNodeId nodeId)
{
    std::map<int, OmnetId>::iterator it = nodeIds_.find(nodeId);
    if(it != nodeIds_.end())
        return it->second;
    return 0;
}

MacNodeId LteBinder::getMacNodeIdFromOmnetId(OmnetId id){
	std::map<int, OmnetId>::iterator it;
	for (it = nodeIds_.begin(); it != nodeIds_.end(); ++it )
	    if (it->second == id)
	        return it->first;
	return 0;
}

LteMacBase* LteBinder::getMacFromMacNodeId(MacNodeId id)
{
    if (id == 0)
        return NULL;

    LteMacBase* mac;
    if (macNodeIdToModule_.find(id) == macNodeIdToModule_.end())
    {
        mac = check_and_cast<LteMacBase*>(getMacByMacNodeId(id));
        macNodeIdToModule_[id] = mac;
    }
    else
    {
        mac = macNodeIdToModule_[id];
    }
    return mac;
}

MacNodeId LteBinder::getNextHop(MacNodeId slaveId)
{
    Enter_Method_Silent("getNextHop");
    if (slaveId >= nextHop_.size())
        throw cRuntimeError("LteBinder::getNextHop(): bad slave id %d", slaveId);
    return nextHop_[slaveId];
}

void LteBinder::registerName(MacNodeId nodeId, const char* moduleName)
{
    int len = strlen(moduleName);
    macNodeIdToModuleName_[nodeId] = new char[len+1];
    strcpy(macNodeIdToModuleName_[nodeId], moduleName);
}

const char* LteBinder::getModuleNameByMacNodeId(MacNodeId nodeId)
{
    if (macNodeIdToModuleName_.find(nodeId) == macNodeIdToModuleName_.end())
        throw cRuntimeError("LteBinder::getModuleNameByMacNodeId - node ID not found");
    return macNodeIdToModuleName_[nodeId];
}

ConnectedUesMap LteBinder::getDeployedUes(MacNodeId localId, Direction dir)
{
    Enter_Method_Silent("getDeployedUes");
    return dMap_[localId];
}

void LteBinder::registerX2Port(X2NodeId nodeId, int port)
{
    if (x2ListeningPorts_.find(nodeId) == x2ListeningPorts_.end() )
    {
        // no port has yet been registered
        std::list<int> ports;
        ports.push_back(port);
        x2ListeningPorts_[nodeId] = ports;
    }
    else
    {
        x2ListeningPorts_[nodeId].push_back(port);
    }
}

int LteBinder::getX2Port(X2NodeId nodeId)
{
    if (x2ListeningPorts_.find(nodeId) == x2ListeningPorts_.end() )
        throw cRuntimeError("LteBinder::getX2Port - No ports available on node %d", nodeId);

    int port = x2ListeningPorts_[nodeId].front();
    x2ListeningPorts_[nodeId].pop_front();
    return port;
}

Cqi LteBinder::meanCqi(std::vector<Cqi> bandCqi,MacNodeId id,Direction dir)
{
    std::vector<Cqi>::iterator it;
    Cqi mean=0;
    for (it=bandCqi.begin();it!=bandCqi.end();++it)
    {
        mean+=*it;
    }
    mean/=bandCqi.size();

    if(mean==0)
        mean = 1;

    return mean;
}

void LteBinder::addD2DCapability(MacNodeId src, MacNodeId dst)
{
    if (src < UE_MIN_ID || src >= macNodeIdCounter_[2] || dst < UE_MIN_ID || dst >= macNodeIdCounter_[2])
        throw cRuntimeError("LteBinder::addD2DCapability - Node Id not valid. Src %d Dst %d", src, dst);

    d2dPeeringCapability_[src][dst] = true;

    // insert initial communication mode
    // TODO make it configurable from NED

    // enable DM only if the two endpoints are served by the same cell
    if (nextHop_[src] == nextHop_[dst])
        d2dPeeringMode_[src][dst] = DM;
    else
        d2dPeeringMode_[src][dst] = IM;

    EV << "LteBinder::addD2DCapability - UE " << src << " may transmit to UE " << dst << " using D2D (current mode " << ((d2dPeeringMode_[src][dst] == DM) ? "DM)" : "IM)") << endl;
}

bool LteBinder::checkD2DCapability(MacNodeId src, MacNodeId dst)
{
    if (src < UE_MIN_ID || src >= macNodeIdCounter_[2] || dst < UE_MIN_ID || dst >= macNodeIdCounter_[2])
        throw cRuntimeError("LteBinder::checkD2DCapability - Node Id not valid. Src %d Dst %d", src, dst);

    if (d2dPeeringCapability_.find(src) != d2dPeeringCapability_.end() && d2dPeeringCapability_[src].find(dst) != d2dPeeringCapability_[src].end())
        return d2dPeeringCapability_[src][dst];
    return false;
}

std::map<MacNodeId, std::map<MacNodeId, LteD2DMode> >* LteBinder::getD2DPeeringModeMap()
{
    return &d2dPeeringMode_;
}

LteD2DMode LteBinder::getD2DMode(MacNodeId src, MacNodeId dst)
{
    if (src < UE_MIN_ID || src >= macNodeIdCounter_[2] || dst < UE_MIN_ID || dst >= macNodeIdCounter_[2])
        throw cRuntimeError("LteBinder::getD2DMode - Node Id not valid. Src %d Dst %d", src, dst);

    return d2dPeeringMode_[src][dst];
}

bool LteBinder::isFrequencyReuseEnabled(MacNodeId nodeId)
{
    // a d2d-enabled UE can use frequency reuse if it can communicate using DM with all its peers
    // in fact, the scheduler does not know to which UE it will communicate when it grants some RBs
    if (d2dPeeringMode_.find(nodeId) == d2dPeeringMode_.end())
        return false;

    std::map<MacNodeId, LteD2DMode>::iterator it = d2dPeeringMode_[nodeId].begin();
    if (it == d2dPeeringMode_[nodeId].end())
        return false;

    for (; it != d2dPeeringMode_[nodeId].end(); ++it)
    {
        if (it->second == IM)
            return false;
    }
    return true;
}


void LteBinder::registerMulticastGroup(MacNodeId nodeId, int32 groupId)
{
    if (multicastGroupMap_.find(nodeId) == multicastGroupMap_.end())
    {
        MulticastGroupIdSet newSet;
        newSet.insert(groupId);
        multicastGroupMap_[nodeId] = newSet;
    }
    else
    {
        multicastGroupMap_[nodeId].insert(groupId);
    }
}

bool LteBinder::isInMulticastGroup(MacNodeId nodeId, int32 groupId)
{
    if (multicastGroupMap_.find(nodeId) == multicastGroupMap_.end())
        return false;   // the node is not enrolled in any group
    if (multicastGroupMap_[nodeId].find(groupId) == multicastGroupMap_[nodeId].end())
        return false;   // the node is not enrolled in the given group

    return true;
}

void LteBinder::updateUeInfoCellId(MacNodeId id, MacCellId newCellId)
{
    std::vector<UeInfo*>::iterator it = ueList_.begin();
    for (; it != ueList_.end(); ++it)
    {
        if ((*it)->id == id)
        {
            (*it)->cellId = newCellId;
            return;
        }
    }
}

void LteBinder::addUeHandoverTriggered(MacNodeId nodeId)
{
    ueHandoverTriggered_.insert(nodeId);
}

bool LteBinder::hasUeHandoverTriggered(MacNodeId nodeId)
{
    if (ueHandoverTriggered_.find(nodeId) == ueHandoverTriggered_.end())
        return false;
    return true;
}

void LteBinder::removeUeHandoverTriggered(MacNodeId nodeId)
{
    ueHandoverTriggered_.erase(nodeId);
}
#include "driver_ipc_shm.h"
#include "../../stdafx.h"
#include "../../driver_vrinputemulator.h"
#include <boost/date_time/posix_time/posix_time_types.hpp>
#include <ipc_protocol.h>
#include <openvr_math.h>

namespace vrinputemulator {
	namespace driver {


		void IpcShmCommunicator::init(CServerDriver* driver) {
			_driver = driver;
			_ipcThreadStopFlag = false;
			_ipcThread = std::thread(_ipcThreadFunc, this, driver);
		}

		void IpcShmCommunicator::shutdown() {
			if (_ipcThreadRunning) {
				_ipcThreadStopFlag = true;
				_ipcThread.join();
			}
		}

		void IpcShmCommunicator::_ipcThreadFunc(IpcShmCommunicator* _this, CServerDriver * driver) {
			_this->_ipcThreadRunning = true;
			LOG(DEBUG) << "CServerDriver::_ipcThreadFunc: thread started";
			try {
				// Create message queue
				boost::interprocess::message_queue::remove(_this->_ipcQueueName.c_str());
				boost::interprocess::message_queue messageQueue(
					boost::interprocess::create_only,
					_this->_ipcQueueName.c_str(),
					100,					//max message number
					sizeof(ipc::Request)    //max message size
				);

				while (!_this->_ipcThreadStopFlag) {
					try {
						ipc::Request message;
						uint64_t recv_size;
						unsigned priority;
						boost::posix_time::ptime timeout = boost::posix_time::microsec_clock::universal_time() + boost::posix_time::milliseconds(50);
						if (messageQueue.timed_receive(&message, sizeof(ipc::Request), recv_size, priority, timeout)) {
							LOG(TRACE) << "CServerDriver::_ipcThreadFunc: IPC request received ( type " << (int)message.type << ")";
							if (recv_size == sizeof(ipc::Request)) {
								switch (message.type) {

								case ipc::RequestType::IPC_ClientConnect:
								{
									try {
										auto queue = std::make_shared<boost::interprocess::message_queue>(boost::interprocess::open_only, message.msg.ipc_ClientConnect.queueName);
										ipc::Reply reply(ipc::ReplyType::IPC_ClientConnect);
										reply.messageId = message.msg.ipc_ClientConnect.messageId;
										reply.msg.ipc_ClientConnect.ipcProcotolVersion = IPC_PROTOCOL_VERSION;
										if (message.msg.ipc_ClientConnect.ipcProcotolVersion == IPC_PROTOCOL_VERSION) {
											auto clientId = _this->_ipcClientIdNext++;
											_this->_ipcEndpoints.insert({ clientId, queue });
											reply.msg.ipc_ClientConnect.clientId = clientId;
											reply.status = ipc::ReplyStatus::Ok;
											LOG(INFO) << "New client connected: endpoint \"" << message.msg.ipc_ClientConnect.queueName << "\", cliendId " << clientId;
										}
										else {
											reply.msg.ipc_ClientConnect.clientId = 0;
											reply.status = ipc::ReplyStatus::InvalidVersion;
											LOG(INFO) << "Client (endpoint \"" << message.msg.ipc_ClientConnect.queueName << "\") reports incompatible ipc version "
												<< message.msg.ipc_ClientConnect.ipcProcotolVersion;
										}
										queue->send(&reply, sizeof(ipc::Reply), 0);
									}
									catch (std::exception& e) {
										LOG(ERROR) << "Error during client connect: " << e.what();
									}
								}
								break;

								case ipc::RequestType::IPC_ClientDisconnect:
								{
									ipc::Reply reply(ipc::ReplyType::GenericReply);
									reply.messageId = message.msg.ipc_ClientDisconnect.messageId;
									auto i = _this->_ipcEndpoints.find(message.msg.ipc_ClientDisconnect.clientId);
									if (i != _this->_ipcEndpoints.end()) {
										reply.status = ipc::ReplyStatus::Ok;
										auto msgQueue = i->second;
										_this->_ipcEndpoints.erase(i);
										LOG(INFO) << "Client disconnected: clientId " << message.msg.ipc_ClientDisconnect.clientId;
										if (reply.messageId != 0) {
											msgQueue->send(&reply, sizeof(ipc::Reply), 0);
										}
									}
									else {
										LOG(ERROR) << "Error during client disconnect: unknown clientID " << message.msg.ipc_ClientDisconnect.clientId;
									}
								}
								break;

								case ipc::RequestType::IPC_Ping:
								{
									LOG(TRACE) << "Ping received: clientId " << message.msg.ipc_Ping.clientId << ", nonce " << message.msg.ipc_Ping.nonce;
									auto i = _this->_ipcEndpoints.find(message.msg.ipc_Ping.clientId);
									if (i != _this->_ipcEndpoints.end()) {
										ipc::Reply reply(ipc::ReplyType::IPC_Ping);
										reply.messageId = message.msg.ipc_Ping.messageId;
										reply.status = ipc::ReplyStatus::Ok;
										reply.msg.ipc_Ping.nonce = message.msg.ipc_Ping.nonce;
										if (reply.messageId != 0) {
											i->second->send(&reply, sizeof(ipc::Reply), 0);
										}
									}
									else {
										LOG(ERROR) << "Error during ping: unknown clientID " << message.msg.ipc_ClientDisconnect.clientId;
									}
								}
								break;

								case ipc::RequestType::OpenVR_ButtonEvent:
								{
									if (vr::VRServerDriverHost()) {
										unsigned iterCount = min(message.msg.ipc_ButtonEvent.eventCount, REQUEST_OPENVR_BUTTONEVENT_MAXCOUNT);
										for (unsigned i = 0; i < iterCount; ++i) {
											auto& e = message.msg.ipc_ButtonEvent.events[i];
											try {
												driver->openvr_buttonEvent(e.deviceId, e.eventType, e.buttonId, e.timeOffset);
											}
											catch (std::exception& e) {
												LOG(ERROR) << "Error in ipc thread: " << e.what();
											}
										}
									}
								}
								break;

								case ipc::RequestType::OpenVR_AxisEvent:
								{
									if (vr::VRServerDriverHost()) {
										for (unsigned i = 0; i < message.msg.ipc_AxisEvent.eventCount; ++i) {
											auto& e = message.msg.ipc_AxisEvent.events[i];
											driver->openvr_axisEvent(e.deviceId, e.axisId, e.axisState);
										}
									}
								}
								break;

								case ipc::RequestType::OpenVR_PoseUpdate:
								{
									if (vr::VRServerDriverHost()) {
										driver->openvr_poseUpdate(message.msg.ipc_PoseUpdate.deviceId, message.msg.ipc_PoseUpdate.pose, message.timestamp);
									}
								}
								break;

								case ipc::RequestType::OpenVR_ProximitySensorEvent:
								{
									driver->openvr_proximityEvent(message.msg.ipc_PoseUpdate.deviceId, message.msg.ovr_ProximitySensorEvent.sensorTriggered);
								}
								break;

								case ipc::RequestType::OpenVR_VendorSpecificEvent:
								{
									driver->openvr_vendorSpecificEvent(message.msg.ovr_VendorSpecificEvent.deviceId, message.msg.ovr_VendorSpecificEvent.eventType,
										message.msg.ovr_VendorSpecificEvent.eventData, message.msg.ovr_VendorSpecificEvent.timeOffset);
								}
								break;

								case ipc::RequestType::VirtualDevices_GetDeviceCount:
								{
									auto i = _this->_ipcEndpoints.find(message.msg.vd_GenericClientMessage.clientId);
									if (i != _this->_ipcEndpoints.end()) {
										ipc::Reply resp(ipc::ReplyType::VirtualDevices_GetDeviceCount);
										resp.messageId = message.msg.vd_GenericClientMessage.messageId;
										resp.status = ipc::ReplyStatus::Ok;
										resp.msg.vd_GetDeviceCount.deviceCount = driver->virtualDevices_getDeviceCount();
										i->second->send(&resp, sizeof(ipc::Reply), 0);
									}
									else {
										LOG(ERROR) << "Error while getting virtual device count: Unknown clientId " << message.msg.vd_AddDevice.clientId;
									}

								}
								break;

								case ipc::RequestType::VirtualDevices_GetDeviceInfo:
								{
									auto i = _this->_ipcEndpoints.find(message.msg.vd_GenericDeviceIdMessage.clientId);
									if (i != _this->_ipcEndpoints.end()) {
										ipc::Reply resp(ipc::ReplyType::VirtualDevices_GetDeviceInfo);
										resp.messageId = message.msg.vd_GenericDeviceIdMessage.messageId;
										if (message.msg.vd_GenericDeviceIdMessage.deviceId >= vr::k_unMaxTrackedDeviceCount) {
											resp.status = ipc::ReplyStatus::InvalidId;
										}
										else {
											auto d = driver->virtualDevices_getDevice(message.msg.vd_GenericDeviceIdMessage.deviceId);
											if (!d) {
												resp.status = ipc::ReplyStatus::NotFound;
											}
											else {
												resp.msg.vd_GetDeviceInfo.virtualDeviceId = message.msg.vd_GenericDeviceIdMessage.deviceId;
												resp.msg.vd_GetDeviceInfo.openvrDeviceId = d->openvrDeviceId();
												resp.msg.vd_GetDeviceInfo.deviceType = d->deviceType();
												strncpy_s(resp.msg.vd_GetDeviceInfo.deviceSerial, d->serialNumber().c_str(), 127);
												resp.msg.vd_GetDeviceInfo.deviceSerial[127] = '\0';
												resp.status = ipc::ReplyStatus::Ok;
											}
										}
										i->second->send(&resp, sizeof(ipc::Reply), 0);
									}
									else {
										LOG(ERROR) << "Error while getting virtual device info: Unknown clientId " << message.msg.vd_AddDevice.clientId;
									}

								}
								break;

								case ipc::RequestType::VirtualDevices_GetDevicePose:
								{
									auto i = _this->_ipcEndpoints.find(message.msg.vd_GenericDeviceIdMessage.clientId);
									if (i != _this->_ipcEndpoints.end()) {
										ipc::Reply resp(ipc::ReplyType::VirtualDevices_GetDevicePose);
										resp.messageId = message.msg.vd_GenericDeviceIdMessage.messageId;
										if (message.msg.vd_GenericDeviceIdMessage.deviceId >= vr::k_unMaxTrackedDeviceCount) {
											resp.status = ipc::ReplyStatus::InvalidId;
										}
										else {
											auto d = driver->virtualDevices_getDevice(message.msg.vd_GenericDeviceIdMessage.deviceId);
											if (!d) {
												resp.status = ipc::ReplyStatus::NotFound;
											}
											else {
												resp.msg.vd_GetDevicePose.pose = d->GetPose();
												resp.status = ipc::ReplyStatus::Ok;
											}
										}
										i->second->send(&resp, sizeof(ipc::Reply), 0);
									}
									else {
										LOG(ERROR) << "Error while getting virtual device pose: Unknown clientId " << message.msg.vd_AddDevice.clientId;
									}
								}
								break;

								case ipc::RequestType::VirtualDevices_GetControllerState:
								{
									auto i = _this->_ipcEndpoints.find(message.msg.vd_GenericDeviceIdMessage.clientId);
									if (i != _this->_ipcEndpoints.end()) {
										ipc::Reply resp(ipc::ReplyType::VirtualDevices_GetControllerState);
										resp.messageId = message.msg.vd_GenericDeviceIdMessage.messageId;
										if (message.msg.vd_GenericDeviceIdMessage.deviceId >= vr::k_unMaxTrackedDeviceCount) {
											resp.status = ipc::ReplyStatus::InvalidId;
										}
										else {
											auto d = driver->virtualDevices_getDevice(message.msg.vd_GenericDeviceIdMessage.deviceId);
											if (!d) {
												resp.status = ipc::ReplyStatus::NotFound;
											}
											else {
												auto c = (vr::IVRControllerComponent*)d->GetComponent(vr::IVRControllerComponent_Version);
												if (c) {
													resp.msg.vd_GetControllerState.controllerState = c->GetControllerState();
													resp.status = ipc::ReplyStatus::Ok;
												}
												else {
													resp.status = ipc::ReplyStatus::InvalidType;
												}
											}
										}
										i->second->send(&resp, sizeof(ipc::Reply), 0);
									}
									else {
										LOG(ERROR) << "Error while getting virtual controller state: Unknown clientId " << message.msg.vd_AddDevice.clientId;
									}

								}
								break;

								case ipc::RequestType::VirtualDevices_AddDevice:
								{
									auto i = _this->_ipcEndpoints.find(message.msg.vd_AddDevice.clientId);
									if (i != _this->_ipcEndpoints.end()) {
										auto result = driver->virtualDevices_addDevice(message.msg.vd_AddDevice.deviceType, message.msg.vd_AddDevice.deviceSerial);
										ipc::Reply resp(ipc::ReplyType::VirtualDevices_AddDevice);
										resp.messageId = message.msg.vd_AddDevice.messageId;
										if (result >= 0) {
											resp.status = ipc::ReplyStatus::Ok;
											resp.msg.vd_AddDevice.virtualDeviceId = (uint32_t)result;
										}
										else if (result == -1) {
											resp.status = ipc::ReplyStatus::TooManyDevices;
										}
										else if (result == -2) {
											resp.status = ipc::ReplyStatus::AlreadyInUse;
											auto d = driver->virtualDevices_findDevice(message.msg.vd_AddDevice.deviceSerial);
											resp.msg.vd_AddDevice.virtualDeviceId = d->virtualDeviceId();
										}
										else if (result == -3) {
											resp.status = ipc::ReplyStatus::InvalidType;
										}
										else {
											resp.status = ipc::ReplyStatus::UnknownError;
										}
										if (resp.status != ipc::ReplyStatus::Ok) {
											LOG(ERROR) << "Error while adding virtual device: Error code " << (int)resp.status;
										}
										if (resp.messageId != 0) {
											i->second->send(&resp, sizeof(ipc::Reply), 0);
										}
									}
									else {
										LOG(ERROR) << "Error while adding virtual device: Unknown clientId " << message.msg.vd_AddDevice.clientId;
									}
								}
								break;

								case ipc::RequestType::VirtualDevices_PublishDevice:
								{
									auto result = driver->virtualDevices_publishDevice(message.msg.vd_GenericDeviceIdMessage.deviceId);
									ipc::Reply resp(ipc::ReplyType::GenericReply);
									resp.messageId = message.msg.vd_GenericDeviceIdMessage.messageId;
									if (result >= 0) {
										resp.status = ipc::ReplyStatus::Ok;
									}
									else if (result == -1) {
										resp.status = ipc::ReplyStatus::InvalidId;
									}
									else if (result == -2) {
										resp.status = ipc::ReplyStatus::NotFound;
									}
									else if (result == -3) {
										resp.status = ipc::ReplyStatus::Ok; // It's already published, let's regard this as "Ok"
									}
									else if (result == -4) {
										resp.status = ipc::ReplyStatus::MissingProperty;
									}
									else {
										resp.status = ipc::ReplyStatus::UnknownError;
									}
									if (resp.status != ipc::ReplyStatus::Ok) {
										LOG(ERROR) << "Error while publishing virtual device: Error code " << (int)resp.status;
									}
									if (resp.messageId != 0) {
										auto i = _this->_ipcEndpoints.find(message.msg.vd_GenericDeviceIdMessage.clientId);
										if (i != _this->_ipcEndpoints.end()) {
											i->second->send(&resp, sizeof(ipc::Reply), 0);
										}
										else {
											LOG(ERROR) << "Error while publishing virtual device: Unknown clientId " << message.msg.vd_GenericDeviceIdMessage.clientId;
										}
									}
								}
								break;

								case ipc::RequestType::VirtualDevices_SetDeviceProperty:
								{
									ipc::Reply resp(ipc::ReplyType::GenericReply);
									resp.messageId = message.msg.vd_SetDeviceProperty.messageId;
									if (message.msg.vd_SetDeviceProperty.virtualDeviceId >= driver->virtualDevices_getDeviceCount()) {
										resp.status = ipc::ReplyStatus::InvalidId;
									}
									else {
										auto device = driver->virtualDevices_getDevice(message.msg.vd_SetDeviceProperty.virtualDeviceId);
										if (!device) {
											resp.status = ipc::ReplyStatus::NotFound;
										}
										else {
											resp.status = ipc::ReplyStatus::Ok;
											switch (message.msg.vd_SetDeviceProperty.valueType) {
											case DevicePropertyValueType::BOOL:
												LOG(TRACE) << "CTrackedDeviceDriver[" << device->serialNumber() << "]::setTrackedDeviceProperty("
													<< message.msg.vd_SetDeviceProperty.deviceProperty << ", " << message.msg.vd_SetDeviceProperty.value.boolValue << ")";
												device->setTrackedDeviceProperty(message.msg.vd_SetDeviceProperty.deviceProperty, message.msg.vd_SetDeviceProperty.value.boolValue);
												break;
											case DevicePropertyValueType::FLOAT:
												LOG(TRACE) << "CTrackedDeviceDriver[" << device->serialNumber() << "]::setTrackedDeviceProperty("
													<< message.msg.vd_SetDeviceProperty.deviceProperty << ", " << message.msg.vd_SetDeviceProperty.value.floatValue << ")";
												device->setTrackedDeviceProperty(message.msg.vd_SetDeviceProperty.deviceProperty, message.msg.vd_SetDeviceProperty.value.floatValue);
												break;
											case DevicePropertyValueType::INT32:
												LOG(TRACE) << "CTrackedDeviceDriver[" << device->serialNumber() << "]::setTrackedDeviceProperty("
													<< message.msg.vd_SetDeviceProperty.deviceProperty << ", " << message.msg.vd_SetDeviceProperty.value.int32Value << ")";
												device->setTrackedDeviceProperty(message.msg.vd_SetDeviceProperty.deviceProperty, message.msg.vd_SetDeviceProperty.value.int32Value);
												break;
											case DevicePropertyValueType::MATRIX34:
												LOG(TRACE) << "CTrackedDeviceDriver[" << device->serialNumber() << "]::setTrackedDeviceProperty("
													<< message.msg.vd_SetDeviceProperty.deviceProperty << ", <matrix34> )";
												device->setTrackedDeviceProperty(message.msg.vd_SetDeviceProperty.deviceProperty, message.msg.vd_SetDeviceProperty.value.matrix34Value);
												break;
											case DevicePropertyValueType::MATRIX44:
												LOG(TRACE) << "CTrackedDeviceDriver[" << device->serialNumber() << "]::setTrackedDeviceProperty("
													<< message.msg.vd_SetDeviceProperty.deviceProperty << ", <matrix44> )";
												device->setTrackedDeviceProperty(message.msg.vd_SetDeviceProperty.deviceProperty, message.msg.vd_SetDeviceProperty.value.matrix44Value);
												break;
											case DevicePropertyValueType::VECTOR3:
												LOG(TRACE) << "CTrackedDeviceDriver[" << device->serialNumber() << "]::setTrackedDeviceProperty("
													<< message.msg.vd_SetDeviceProperty.deviceProperty << ", <vector3> )";
												device->setTrackedDeviceProperty(message.msg.vd_SetDeviceProperty.deviceProperty, message.msg.vd_SetDeviceProperty.value.vector3Value);
												break;
											case DevicePropertyValueType::VECTOR4:
												LOG(TRACE) << "CTrackedDeviceDriver[" << device->serialNumber() << "]::setTrackedDeviceProperty("
													<< message.msg.vd_SetDeviceProperty.deviceProperty << ", <vector4> )";
												device->setTrackedDeviceProperty(message.msg.vd_SetDeviceProperty.deviceProperty, message.msg.vd_SetDeviceProperty.value.vector4Value);
												break;
											case DevicePropertyValueType::STRING:
												LOG(TRACE) << "CTrackedDeviceDriver[" << device->serialNumber() << "]::setTrackedDeviceProperty("
													<< message.msg.vd_SetDeviceProperty.deviceProperty << ", " << message.msg.vd_SetDeviceProperty.value.stringValue << ")";
												device->setTrackedDeviceProperty(message.msg.vd_SetDeviceProperty.deviceProperty, std::string(message.msg.vd_SetDeviceProperty.value.stringValue));
												break;
											case DevicePropertyValueType::UINT64:
												LOG(TRACE) << "CTrackedDeviceDriver[" << device->serialNumber() << "]::setTrackedDeviceProperty("
													<< message.msg.vd_SetDeviceProperty.deviceProperty << ", " << message.msg.vd_SetDeviceProperty.value.uint64Value << ")";
												device->setTrackedDeviceProperty(message.msg.vd_SetDeviceProperty.deviceProperty, message.msg.vd_SetDeviceProperty.value.uint64Value);
												break;
											default:
												resp.status = ipc::ReplyStatus::InvalidType;
												break;
											}
										}
									}
									if (resp.status != ipc::ReplyStatus::Ok) {
										LOG(ERROR) << "Error while setting device property: Error code " << (int)resp.status;
									}
									if (resp.messageId != 0) {
										auto i = _this->_ipcEndpoints.find(message.msg.vd_SetDeviceProperty.clientId);
										if (i != _this->_ipcEndpoints.end()) {
											i->second->send(&resp, sizeof(ipc::Reply), 0);
										}
										else {
											LOG(ERROR) << "Error while setting device property: Unknown clientId " << message.msg.vd_SetDeviceProperty.clientId;
										}
									}
								}
								break;

								case ipc::RequestType::VirtualDevices_RemoveDeviceProperty:
								{
									ipc::Reply resp(ipc::ReplyType::GenericReply);
									resp.messageId = message.msg.vd_RemoveDeviceProperty.messageId;
									if (message.msg.vd_RemoveDeviceProperty.virtualDeviceId >= driver->virtualDevices_getDeviceCount()) {
										resp.status = ipc::ReplyStatus::InvalidId;
									}
									else {
										auto device = driver->virtualDevices_getDevice(message.msg.vd_RemoveDeviceProperty.virtualDeviceId);
										if (!device) {
											resp.status = ipc::ReplyStatus::NotFound;
										}
										else {
											LOG(TRACE) << "CTrackedDeviceDriver[" << device->serialNumber() << "]::removeTrackedDeviceProperty("
												<< message.msg.vd_RemoveDeviceProperty.deviceProperty << ")";
											device->removeTrackedDeviceProperty(message.msg.vd_RemoveDeviceProperty.deviceProperty);
											resp.status = ipc::ReplyStatus::Ok;
										}
									}
									if (resp.status != ipc::ReplyStatus::Ok) {
										LOG(ERROR) << "Error while removing device property: Error code " << (int)resp.status;
									}
									if (resp.messageId != 0) {
										auto i = _this->_ipcEndpoints.find(message.msg.vd_RemoveDeviceProperty.clientId);
										if (i != _this->_ipcEndpoints.end()) {
											i->second->send(&resp, sizeof(ipc::Reply), 0);
										}
										else {
											LOG(ERROR) << "Error while removing device property: Unknown clientId " << message.msg.vd_RemoveDeviceProperty.clientId;
										}
									}
								}
								break;

								case ipc::RequestType::VirtualDevices_SetDevicePose:
								{
									ipc::Reply resp(ipc::ReplyType::GenericReply);
									resp.messageId = message.msg.vd_SetDevicePose.messageId;
									if (message.msg.vd_SetDevicePose.virtualDeviceId >= driver->virtualDevices_getDeviceCount()) {
										resp.status = ipc::ReplyStatus::InvalidId;
									}
									else {
										auto device = driver->virtualDevices_getDevice(message.msg.vd_SetDevicePose.virtualDeviceId);
										if (!device) {
											resp.status = ipc::ReplyStatus::NotFound;
										}
										else {
											auto now = std::chrono::duration_cast <std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
											auto diff = 0.0;
											if (message.timestamp < now) {
												diff = ((double)now - message.timestamp) / 1000.0;
											}
											device->updatePose(message.msg.vd_SetDevicePose.pose, -diff);
											resp.status = ipc::ReplyStatus::Ok;
										}
									}
									if (resp.status != ipc::ReplyStatus::Ok) {
										LOG(ERROR) << "Error while updating device pose: Error code " << (int)resp.status;
									}
									if (resp.messageId != 0) {
										auto i = _this->_ipcEndpoints.find(message.msg.vd_SetDevicePose.clientId);
										if (i != _this->_ipcEndpoints.end()) {
											i->second->send(&resp, sizeof(ipc::Reply), 0);
										}
										else {
											LOG(ERROR) << "Error while updating device pose: Unknown clientId " << message.msg.vd_SetDevicePose.clientId;
										}
									}
								}
								break;

								case ipc::RequestType::VirtualDevices_SetControllerState:
								{
									ipc::Reply resp(ipc::ReplyType::GenericReply);
									resp.messageId = message.msg.vd_SetControllerState.messageId;
									if (message.msg.vd_SetControllerState.virtualDeviceId >= driver->virtualDevices_getDeviceCount()) {
										resp.status = ipc::ReplyStatus::InvalidId;
									}
									else {
										auto device = driver->virtualDevices_getDevice(message.msg.vd_SetControllerState.virtualDeviceId);
										if (!device) {
											resp.status = ipc::ReplyStatus::NotFound;
										}
										else {
											resp.status = ipc::ReplyStatus::Ok;
											if (device->deviceType() == VirtualDeviceType::TrackedController) {
												auto controller = (CTrackedControllerDriver*)device;
												auto now = std::chrono::duration_cast <std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
												auto diff = 0.0;
												if (message.timestamp < now) {
													diff = ((double)now - message.timestamp) / 1000.0;
												}
												controller->updateControllerState(message.msg.vd_SetControllerState.controllerState, -diff);
											}
											else {
												resp.status = ipc::ReplyStatus::InvalidType;
											}
										}
									}
									if (resp.status != ipc::ReplyStatus::Ok) {
										LOG(ERROR) << "Error while updating controller state: Error code " << (int)resp.status;
									}
									if (resp.messageId != 0) {
										auto i = _this->_ipcEndpoints.find(message.msg.vd_SetControllerState.clientId);
										if (i != _this->_ipcEndpoints.end()) {
											i->second->send(&resp, sizeof(ipc::Reply), 0);
										}
										else {
											LOG(ERROR) << "Error while updating controller state: Unknown clientId " << message.msg.vd_SetControllerState.clientId;
										}
									}
								}
								break;

								case ipc::RequestType::DeviceManipulation_GetDeviceInfo:
								{
									ipc::Reply resp(ipc::ReplyType::GenericReply);
									resp.messageId = message.msg.vd_GenericDeviceIdMessage.messageId;
									if (message.msg.vd_GenericDeviceIdMessage.deviceId >= vr::k_unMaxTrackedDeviceCount) {
										resp.status = ipc::ReplyStatus::InvalidId;
									}
									else {
										OpenvrDeviceManipulationInfo* info = driver->deviceManipulation_getInfo(message.msg.vd_GenericDeviceIdMessage.deviceId);
										if (!info) {
											resp.status = ipc::ReplyStatus::NotFound;
										}
										else {
											resp.status = ipc::ReplyStatus::Ok;
											resp.msg.dm_deviceInfo.deviceId = message.msg.vd_GenericDeviceIdMessage.deviceId;
											resp.msg.dm_deviceInfo.deviceMode = info->deviceMode();
											resp.msg.dm_deviceInfo.deviceClass = info->deviceClass();
											resp.msg.dm_deviceInfo.offsetsEnabled = info->areOffsetsEnabled();
											resp.msg.dm_deviceInfo.buttonMappingEnabled = info->buttonMappingEnabled();
											resp.msg.dm_deviceInfo.redirectSuspended = info->redirectSuspended();
										}
									}
									if (resp.status != ipc::ReplyStatus::Ok) {
										LOG(ERROR) << "Error while updating device button mapping: Error code " << (int)resp.status;
									}
									if (resp.messageId != 0) {
										auto i = _this->_ipcEndpoints.find(message.msg.dm_ButtonMapping.clientId);
										if (i != _this->_ipcEndpoints.end()) {
											i->second->send(&resp, sizeof(ipc::Reply), 0);
										}
										else {
											LOG(ERROR) << "Error while updating device button mapping: Unknown clientId " << message.msg.dm_ButtonMapping.clientId;
										}
									}
								}
								break;

								case ipc::RequestType::DeviceManipulation_ButtonMapping:
								{
									ipc::Reply resp(ipc::ReplyType::GenericReply);
									resp.messageId = message.msg.dm_ButtonMapping.messageId;
									if (message.msg.dm_ButtonMapping.deviceId >= vr::k_unMaxTrackedDeviceCount) {
										resp.status = ipc::ReplyStatus::InvalidId;
									}
									else {
										OpenvrDeviceManipulationInfo* info = driver->deviceManipulation_getInfo(message.msg.dm_ButtonMapping.deviceId);
										if (!info) {
											resp.status = ipc::ReplyStatus::NotFound;
										}
										else {
											resp.status = ipc::ReplyStatus::Ok;
											if (message.msg.dm_ButtonMapping.enableMapping > 0) {
												info->setButtonMappingEnabled(message.msg.dm_ButtonMapping.enableMapping == 1 ? true : false);
											}
											switch (message.msg.dm_ButtonMapping.mappingOperation) {
											case 0:
												break;
											case 1:
												for (unsigned i = 0; i < message.msg.dm_ButtonMapping.mappingCount; ++i) {
													info->addButtonMapping(message.msg.dm_ButtonMapping.buttonMappings[i * 2], message.msg.dm_ButtonMapping.buttonMappings[i * 2 + 1]);
												}
												break;
											case 2:
												for (unsigned i = 0; i < message.msg.dm_ButtonMapping.mappingCount; ++i) {
													info->eraseButtonMapping(message.msg.dm_ButtonMapping.buttonMappings[i]);
												}
												break;
											case 3:
												info->eraseAllButtonMappings();
												break;
											default:
												resp.status = ipc::ReplyStatus::InvalidOperation;
												break;
											}
										}
									}
									if (resp.status != ipc::ReplyStatus::Ok) {
										LOG(ERROR) << "Error while updating device button mapping: Error code " << (int)resp.status;
									}
									if (resp.messageId != 0) {
										auto i = _this->_ipcEndpoints.find(message.msg.dm_ButtonMapping.clientId);
										if (i != _this->_ipcEndpoints.end()) {
											i->second->send(&resp, sizeof(ipc::Reply), 0);
										}
										else {
											LOG(ERROR) << "Error while updating device button mapping: Unknown clientId " << message.msg.dm_ButtonMapping.clientId;
										}
									}
								}
								break;

								case ipc::RequestType::DeviceManipulation_GetDeviceOffsets:
								{
									ipc::Reply resp(ipc::ReplyType::DeviceManipulation_GetDeviceOffsets);
									resp.messageId = message.msg.vd_GenericDeviceIdMessage.messageId;
									if (message.msg.vd_GenericDeviceIdMessage.deviceId >= vr::k_unMaxTrackedDeviceCount) {
										resp.status = ipc::ReplyStatus::InvalidId;
									}
									else {
										OpenvrDeviceManipulationInfo* info = driver->deviceManipulation_getInfo(message.msg.vd_GenericDeviceIdMessage.deviceId);
										if (!info) {
											resp.status = ipc::ReplyStatus::NotFound;
										}
										else {
											resp.status = ipc::ReplyStatus::Ok;
											resp.msg.dm_deviceOffsets.deviceId = message.msg.vd_GenericDeviceIdMessage.deviceId;
											resp.msg.dm_deviceOffsets.offsetsEnabled = info->areOffsetsEnabled();
											resp.msg.dm_deviceOffsets.worldFromDriverRotationOffset = info->worldFromDriverRotationOffset();
											resp.msg.dm_deviceOffsets.worldFromDriverTranslationOffset = info->worldFromDriverTranslationOffset();
											resp.msg.dm_deviceOffsets.driverFromHeadRotationOffset = info->driverFromHeadRotationOffset();
											resp.msg.dm_deviceOffsets.driverFromHeadTranslationOffset = info->driverFromHeadTranslationOffset();
											resp.msg.dm_deviceOffsets.driverFromHeadTranslationOffset = info->driverFromHeadTranslationOffset();
											resp.msg.dm_deviceOffsets.deviceRotationOffset = info->deviceRotationOffset();
											resp.msg.dm_deviceOffsets.deviceTranslationOffset = info->deviceTranslationOffset();
										}
									}
									if (resp.status != ipc::ReplyStatus::Ok) {
										LOG(ERROR) << "Error while updating device button mapping: Error code " << (int)resp.status;
									}
									if (resp.messageId != 0) {
										auto i = _this->_ipcEndpoints.find(message.msg.dm_ButtonMapping.clientId);
										if (i != _this->_ipcEndpoints.end()) {
											i->second->send(&resp, sizeof(ipc::Reply), 0);
										}
										else {
											LOG(ERROR) << "Error while updating device button mapping: Unknown clientId " << message.msg.dm_ButtonMapping.clientId;
										}
									}
								}
								break;

								case ipc::RequestType::DeviceManipulation_SetDeviceOffsets:
								{
									ipc::Reply resp(ipc::ReplyType::GenericReply);
									resp.messageId = message.msg.dm_DeviceOffsets.messageId;
									if (message.msg.dm_DeviceOffsets.deviceId >= vr::k_unMaxTrackedDeviceCount) {
										resp.status = ipc::ReplyStatus::InvalidId;
									}
									else {
										OpenvrDeviceManipulationInfo* info = driver->deviceManipulation_getInfo(message.msg.dm_DeviceOffsets.deviceId);
										if (!info) {
											resp.status = ipc::ReplyStatus::NotFound;
										}
										else {
											resp.status = ipc::ReplyStatus::Ok;
											if (message.msg.dm_DeviceOffsets.enableOffsets > 0) {
												info->enableOffsets(message.msg.dm_DeviceOffsets.enableOffsets == 1 ? true : false);
											}
											switch (message.msg.dm_DeviceOffsets.offsetOperation) {
											case 0:
												if (message.msg.dm_DeviceOffsets.worldFromDriverRotationOffsetValid) {
													info->worldFromDriverRotationOffset() = message.msg.dm_DeviceOffsets.worldFromDriverRotationOffset;
												}
												if (message.msg.dm_DeviceOffsets.worldFromDriverTranslationOffsetValid) {
													info->worldFromDriverTranslationOffset() = message.msg.dm_DeviceOffsets.worldFromDriverTranslationOffset;
												}
												if (message.msg.dm_DeviceOffsets.driverFromHeadRotationOffsetValid) {
													info->driverFromHeadRotationOffset() = message.msg.dm_DeviceOffsets.driverFromHeadRotationOffset;
												}
												if (message.msg.dm_DeviceOffsets.driverFromHeadTranslationOffsetValid) {
													info->driverFromHeadTranslationOffset() = message.msg.dm_DeviceOffsets.driverFromHeadTranslationOffset;
												}
												if (message.msg.dm_DeviceOffsets.deviceRotationOffsetValid) {
													info->deviceRotationOffset() = message.msg.dm_DeviceOffsets.deviceRotationOffset;
												}
												if (message.msg.dm_DeviceOffsets.deviceTranslationOffsetValid) {
													info->deviceTranslationOffset() = message.msg.dm_DeviceOffsets.deviceTranslationOffset;
												}
												break;
											case 1:
												if (message.msg.dm_DeviceOffsets.worldFromDriverRotationOffsetValid) {
													info->worldFromDriverRotationOffset() = message.msg.dm_DeviceOffsets.worldFromDriverRotationOffset * info->worldFromDriverRotationOffset();
												}
												if (message.msg.dm_DeviceOffsets.worldFromDriverTranslationOffsetValid) {
													info->worldFromDriverTranslationOffset() = info->worldFromDriverTranslationOffset() + message.msg.dm_DeviceOffsets.worldFromDriverTranslationOffset;
												}
												if (message.msg.dm_DeviceOffsets.driverFromHeadRotationOffsetValid) {
													info->driverFromHeadRotationOffset() = message.msg.dm_DeviceOffsets.driverFromHeadRotationOffset * info->driverFromHeadRotationOffset();
												}
												if (message.msg.dm_DeviceOffsets.driverFromHeadTranslationOffsetValid) {
													info->driverFromHeadTranslationOffset() = info->driverFromHeadTranslationOffset() + message.msg.dm_DeviceOffsets.driverFromHeadTranslationOffset;
												}
												if (message.msg.dm_DeviceOffsets.deviceRotationOffsetValid) {
													info->deviceRotationOffset() = message.msg.dm_DeviceOffsets.deviceRotationOffset * info->deviceRotationOffset();
												}
												if (message.msg.dm_DeviceOffsets.deviceTranslationOffsetValid) {
													info->deviceTranslationOffset() = info->deviceTranslationOffset() + message.msg.dm_DeviceOffsets.deviceTranslationOffset;
												}
												break;
											}
										}
									}
									if (resp.status != ipc::ReplyStatus::Ok) {
										LOG(ERROR) << "Error while updating device pose offset: Error code " << (int)resp.status;
									}
									if (resp.messageId != 0) {
										auto i = _this->_ipcEndpoints.find(message.msg.dm_DeviceOffsets.clientId);
										if (i != _this->_ipcEndpoints.end()) {
											i->second->send(&resp, sizeof(ipc::Reply), 0);
										}
										else {
											LOG(ERROR) << "Error while updating device pose offset: Unknown clientId " << message.msg.dm_DeviceOffsets.clientId;
										}
									}
								}
								break;

								case ipc::RequestType::DeviceManipulation_DefaultMode:
								{
									ipc::Reply resp(ipc::ReplyType::GenericReply);
									resp.messageId = message.msg.vd_GenericDeviceIdMessage.messageId;
									if (message.msg.vd_GenericDeviceIdMessage.deviceId >= vr::k_unMaxTrackedDeviceCount) {
										resp.status = ipc::ReplyStatus::InvalidId;
									}
									else {
										OpenvrDeviceManipulationInfo* info = driver->deviceManipulation_getInfo(message.msg.vd_GenericDeviceIdMessage.deviceId);
										if (!info) {
											resp.status = ipc::ReplyStatus::NotFound;
										}
										else {
											info->setDefaultMode();
											resp.status = ipc::ReplyStatus::Ok;
										}
									}
									if (resp.status != ipc::ReplyStatus::Ok) {
										LOG(ERROR) << "Error while updating device pose offset: Error code " << (int)resp.status;
									}
									if (resp.messageId != 0) {
										auto i = _this->_ipcEndpoints.find(message.msg.vd_GenericDeviceIdMessage.clientId);
										if (i != _this->_ipcEndpoints.end()) {
											i->second->send(&resp, sizeof(ipc::Reply), 0);
										}
										else {
											LOG(ERROR) << "Error while updating device pose offset: Unknown clientId " << message.msg.vd_GenericDeviceIdMessage.clientId;
										}
									}
								}
								break;

								case ipc::RequestType::DeviceManipulation_RedirectMode:
								{
									ipc::Reply resp(ipc::ReplyType::GenericReply);
									resp.messageId = message.msg.dm_RedirectMode.messageId;
									if (message.msg.dm_RedirectMode.deviceId >= vr::k_unMaxTrackedDeviceCount) {
										resp.status = ipc::ReplyStatus::InvalidId;
									}
									else {
										OpenvrDeviceManipulationInfo* info = driver->deviceManipulation_getInfo(message.msg.dm_RedirectMode.deviceId);
										if (!info) {
											resp.status = ipc::ReplyStatus::NotFound;
										}
										else {
											OpenvrDeviceManipulationInfo* infoTarget = driver->deviceManipulation_getInfo(message.msg.dm_RedirectMode.targetId);
											if (info && (info->deviceMode() == 0 || info->deviceMode() == 1)
												&& infoTarget && (infoTarget->deviceMode() == 0 || infoTarget->deviceMode() == 1)) {
												info->setRedirectMode(false, infoTarget);
												infoTarget->setRedirectMode(true, info);
												resp.status = ipc::ReplyStatus::Ok;
											}
											else {
												resp.status = ipc::ReplyStatus::UnknownError;
											}
										}
									}
									if (resp.status != ipc::ReplyStatus::Ok) {
										LOG(ERROR) << "Error while updating device pose offset: Error code " << (int)resp.status;
									}
									if (resp.messageId != 0) {
										auto i = _this->_ipcEndpoints.find(message.msg.dm_RedirectMode.clientId);
										if (i != _this->_ipcEndpoints.end()) {
											i->second->send(&resp, sizeof(ipc::Reply), 0);
										}
										else {
											LOG(ERROR) << "Error while updating device pose offset: Unknown clientId " << message.msg.dm_RedirectMode.clientId;
										}
									}
								}
								break;

								case ipc::RequestType::DeviceManipulation_SwapMode:
								{
									ipc::Reply resp(ipc::ReplyType::GenericReply);
									resp.messageId = message.msg.dm_SwapMode.messageId;
									if (message.msg.dm_SwapMode.deviceId >= vr::k_unMaxTrackedDeviceCount) {
										resp.status = ipc::ReplyStatus::InvalidId;
									}
									else {
										OpenvrDeviceManipulationInfo* info = driver->deviceManipulation_getInfo(message.msg.dm_SwapMode.deviceId);
										if (!info) {
											resp.status = ipc::ReplyStatus::NotFound;
										}
										else {
											OpenvrDeviceManipulationInfo* infoTarget = driver->deviceManipulation_getInfo(message.msg.dm_SwapMode.targetId);
											if (info && (info->deviceMode() == 0 || info->deviceMode() == 1)
												&& infoTarget && (infoTarget->deviceMode() == 0 || infoTarget->deviceMode() == 1)) {
												info->setSwapMode(infoTarget);
												infoTarget->setSwapMode(info);
												resp.status = ipc::ReplyStatus::Ok;
											}
											else {
												resp.status = ipc::ReplyStatus::UnknownError;
											}
										}
									}
									if (resp.status != ipc::ReplyStatus::Ok) {
										LOG(ERROR) << "Error while updating device pose offset: Error code " << (int)resp.status;
									}
									if (resp.messageId != 0) {
										auto i = _this->_ipcEndpoints.find(message.msg.dm_SwapMode.clientId);
										if (i != _this->_ipcEndpoints.end()) {
											i->second->send(&resp, sizeof(ipc::Reply), 0);
										}
										else {
											LOG(ERROR) << "Error while updating device pose offset: Unknown clientId " << message.msg.dm_SwapMode.clientId;
										}
									}
								}
								break;

								case ipc::RequestType::DeviceManipulation_MotionCompensationMode:
								{
									ipc::Reply resp(ipc::ReplyType::GenericReply);
									resp.messageId = message.msg.dm_MotionCompensationMode.messageId;
									if (message.msg.dm_MotionCompensationMode.deviceId >= vr::k_unMaxTrackedDeviceCount) {
										resp.status = ipc::ReplyStatus::InvalidId;
									}
									else {
										OpenvrDeviceManipulationInfo* info = driver->deviceManipulation_getInfo(message.msg.dm_MotionCompensationMode.deviceId);
										if (!info) {
											resp.status = ipc::ReplyStatus::NotFound;
										}
										else {
											auto serverDriver = CServerDriver::getInstance();
											if (serverDriver) {
												serverDriver->setMotionCompensationVelAccMode(message.msg.dm_MotionCompensationMode.velAccCompensationMode);
												info->setMotionCompensationMode();
												resp.status = ipc::ReplyStatus::Ok;
											}
											else {
												resp.status = ipc::ReplyStatus::UnknownError;
											}
										}
									}
									if (resp.status != ipc::ReplyStatus::Ok) {
										LOG(ERROR) << "Error while updating device pose offset: Error code " << (int)resp.status;
									}
									if (resp.messageId != 0) {
										auto i = _this->_ipcEndpoints.find(message.msg.dm_MotionCompensationMode.clientId);
										if (i != _this->_ipcEndpoints.end()) {
											i->second->send(&resp, sizeof(ipc::Reply), 0);
										}
										else {
											LOG(ERROR) << "Error while updating device pose offset: Unknown clientId " << message.msg.dm_MotionCompensationMode.clientId;
										}
									}
								}
								break;

								case ipc::RequestType::DeviceManipulation_StepDetectionMode:
								{
									ipc::Reply resp(ipc::ReplyType::GenericReply);
									resp.messageId = message.msg.dm_StepDetectionMode.messageId;
									auto serverDriver = CServerDriver::getInstance();
									if (serverDriver) {
										if (message.msg.dm_StepDetectionMode.stepDetectOperation == 1) { 	
											if ( message.msg.dm_StepDetectionMode.enableStepDetect == 1) {
												serverDriver->enableStepDetection(true);
											} else {
												serverDriver->enableStepDetection(false);
											}
										}
										else if (message.msg.dm_StepDetectionMode.stepDetectOperation == 2)
										{
											serverDriver->setStepAcceleration(message.msg.dm_StepDetectionMode.stepAcceleration);
										}
										else if (message.msg.dm_StepDetectionMode.stepDetectOperation == 3)
										{
											serverDriver->setStepSpeed(message.msg.dm_StepDetectionMode.stepSpeed);
										}
										else if (message.msg.dm_StepDetectionMode.stepDetectOperation == 4)
										{
											serverDriver->setStepIntSec(message.msg.dm_StepDetectionMode.stepIntSec);
										}
										else if (message.msg.dm_StepDetectionMode.stepDetectOperation == 5)
										{
											serverDriver->setHMDThreshold(message.msg.dm_StepDetectionMode.hmdThreshold);
										}
										else if (message.msg.dm_StepDetectionMode.stepDetectOperation == 6)
										{
											serverDriver->setHandThreshold(message.msg.dm_StepDetectionMode.handThreshold);
										}
										resp.status = ipc::ReplyStatus::Ok;
									}
									else {
										resp.status = ipc::ReplyStatus::UnknownError;
									}
									if (resp.status != ipc::ReplyStatus::Ok) {
										LOG(ERROR) << "Error while updating step detect: Error code " << (int)resp.status;
									}
									if (resp.messageId != 0) {
										auto i = _this->_ipcEndpoints.find(message.msg.dm_StepDetectionMode.clientId);
										if (i != _this->_ipcEndpoints.end()) {
											i->second->send(&resp, sizeof(ipc::Reply), 0);
										}
										else {
											LOG(ERROR) << "Error while updating step detect: Unknown clientId " << message.msg.dm_StepDetectionMode.clientId;
										}
									}
								}
								break;

								case ipc::RequestType::DeviceManipulation_FakeDisconnectedMode:
								{
									ipc::Reply resp(ipc::ReplyType::GenericReply);
									resp.messageId = message.msg.vd_GenericDeviceIdMessage.messageId;
									if (message.msg.vd_GenericDeviceIdMessage.deviceId >= vr::k_unMaxTrackedDeviceCount) {
										resp.status = ipc::ReplyStatus::InvalidId;
									}
									else {
										OpenvrDeviceManipulationInfo* info = driver->deviceManipulation_getInfo(message.msg.vd_GenericDeviceIdMessage.deviceId);
										if (!info) {
											resp.status = ipc::ReplyStatus::NotFound;
										}
										else {
											info->setFakeDisconnectedMode();
											resp.status = ipc::ReplyStatus::Ok;
										}
									}
									if (resp.status != ipc::ReplyStatus::Ok) {
										LOG(ERROR) << "Error while updating device pose offset: Error code " << (int)resp.status;
									}
									if (resp.messageId != 0) {
										auto i = _this->_ipcEndpoints.find(message.msg.vd_GenericDeviceIdMessage.clientId);
										if (i != _this->_ipcEndpoints.end()) {
											i->second->send(&resp, sizeof(ipc::Reply), 0);
										}
										else {
											LOG(ERROR) << "Error while updating device pose offset: Unknown clientId " << message.msg.vd_GenericDeviceIdMessage.clientId;
										}
									}
								}
								break;

								case ipc::RequestType::DeviceManipulation_TriggerHapticPulse:
								{
									ipc::Reply resp(ipc::ReplyType::GenericReply);
									resp.messageId = message.msg.dm_triggerHapticPulse.messageId;
									if (message.msg.dm_triggerHapticPulse.deviceId >= vr::k_unMaxTrackedDeviceCount) {
										resp.status = ipc::ReplyStatus::InvalidId;
									}
									else {
										OpenvrDeviceManipulationInfo* info = driver->deviceManipulation_getInfo(message.msg.dm_triggerHapticPulse.deviceId);
										if (!info) {
											resp.status = ipc::ReplyStatus::NotFound;
										}
										else {
											info->triggerHapticPulse(message.msg.dm_triggerHapticPulse.axisId, message.msg.dm_triggerHapticPulse.durationMicroseconds, message.msg.dm_triggerHapticPulse.directMode);
											resp.status = ipc::ReplyStatus::Ok;
										}
									}
									if (resp.status != ipc::ReplyStatus::Ok) {
										LOG(ERROR) << "Error while triggering haptic pulse: Error code " << (int)resp.status;
									}
									if (resp.messageId != 0) {
										auto i = _this->_ipcEndpoints.find(message.msg.dm_triggerHapticPulse.clientId);
										if (i != _this->_ipcEndpoints.end()) {
											i->second->send(&resp, sizeof(ipc::Reply), 0);
										}
										else {
											LOG(ERROR) << "Error while triggering haptic pulse: Unknown clientId " << message.msg.dm_triggerHapticPulse.clientId;
										}
									}
								}
								break;

								case ipc::RequestType::DeviceManipulation_SetMotionCompensationProperties:
								{
									ipc::Reply resp(ipc::ReplyType::GenericReply);
									resp.messageId = message.msg.dm_SetMotionCompensationProperties.messageId;
									auto serverDriver = CServerDriver::getInstance();
									if (serverDriver) {
										serverDriver->setMotionCompensationVelAccMode(message.msg.dm_SetMotionCompensationProperties.velAccCompensationMode);
										resp.status = ipc::ReplyStatus::Ok;
									}
									else {
										resp.status = ipc::ReplyStatus::UnknownError;
									}
									if (resp.status != ipc::ReplyStatus::Ok) {
										LOG(ERROR) << "Error while setting motion compensation properties: Error code " << (int)resp.status;
									}
									if (resp.messageId != 0) {
										auto i = _this->_ipcEndpoints.find(message.msg.dm_SetMotionCompensationProperties.clientId);
										if (i != _this->_ipcEndpoints.end()) {
											i->second->send(&resp, sizeof(ipc::Reply), 0);
										}
										else {
											LOG(ERROR) << "Error while setting motion compensation properties: Unknown clientId " << message.msg.dm_SetMotionCompensationProperties.clientId;
										}
									}
								}
								break;

								default:
									LOG(ERROR) << "Error in ipc server receive loop: Unknown message type (" << (int)message.type << ")";
									break;
								}
							}
							else {
								LOG(ERROR) << "Error in ipc server receive loop: received size is wrong (" << recv_size << " != " << sizeof(ipc::Request) << ")";
							}
						}
					}
					catch (std::exception& ex) {
						LOG(ERROR) << "Exception caught in ipc server receive loop: " << ex.what();
					}
				}
				boost::interprocess::message_queue::remove(_this->_ipcQueueName.c_str());
			}
			catch (std::exception& ex) {
				LOG(ERROR) << "Exception caught in ipc server thread: " << ex.what();
			}
			_this->_ipcThreadRunning = false;
			LOG(DEBUG) << "CServerDriver::_ipcThreadFunc: thread stopped";
		}


	} // end namespace driver
} // end namespace vrinputemulator


#include "stdafx.h"
#include "driver_vrwalkinplace.h"
#include <boost/date_time/posix_time/posix_time_types.hpp>
#include <boost/lexical_cast.hpp>
#include <ipc_protocol.h>
#include <openvr_math.h>
#include <MinHook.h>
#include <map>
#include <vector>


namespace vrwalkinplace {
	namespace driver {


#define CREATE_MH_HOOK(detourInfo, detourFunc, logName, objPtr, vtableOffset) \
	detourInfo.targetFunc = (*((void***)objPtr))[vtableOffset]; \
	mhError = MH_CreateHook(detourInfo.targetFunc, (void*)&detourFunc, reinterpret_cast<LPVOID*>(&detourInfo.origFunc)); \
	if (mhError == MH_OK) { \
		mhError = MH_EnableHook(detourInfo.targetFunc); \
		if (mhError == MH_OK) { \
			detourInfo.enabled = true; \
			LOG(INFO) << logName << " hook is enabled"; \
		} else { \
			MH_RemoveHook(detourInfo.targetFunc); \
			LOG(ERROR) << "Error while enabling " << logName << " hook: " << MH_StatusToString(mhError); \
		} \
	} else { \
		LOG(ERROR) << "Error while creating " << logName << " hook: " << MH_StatusToString(mhError); \
	}


#define REMOVE_MH_HOOK(detourInfo) \
	if (detourInfo.enabled) { \
		MH_RemoveHook(detourInfo.targetFunc); \
		detourInfo.enabled = false; \
	}

		CServerDriver* CServerDriver::singleton = nullptr;

		std::map<vr::ITrackedDeviceServerDriver*, std::shared_ptr<OpenvrDeviceManipulationInfo>> CServerDriver::_openvrDeviceInfos;
		OpenvrDeviceManipulationInfo* CServerDriver::_openvrIdToDeviceInfoMap[vr::k_unMaxTrackedDeviceCount]; // index == openvrId

		bool g_stepPoseDetectEnabled = false;

		CServerDriver::_DetourFuncInfo<_DetourTrackedDeviceAdded_t> CServerDriver::_deviceAddedDetour;
		CServerDriver::_DetourFuncInfo<_DetourTrackedDevicePoseUpdated_t> CServerDriver::_poseUpatedDetour;
		CServerDriver::_DetourFuncInfo<_DetourTrackedDeviceButtonPressed_t> CServerDriver::_buttonPressedDetour;
		CServerDriver::_DetourFuncInfo<_DetourTrackedDeviceButtonUnpressed_t> CServerDriver::_buttonUnpressedDetour;
		CServerDriver::_DetourFuncInfo<_DetourTrackedDeviceButtonTouched_t> CServerDriver::_buttonTouchedDetour;
		CServerDriver::_DetourFuncInfo<_DetourTrackedDeviceButtonUntouched_t> CServerDriver::_buttonUntouchedDetour;
		CServerDriver::_DetourFuncInfo<_DetourTrackedDeviceAxisUpdated_t> CServerDriver::_axisUpdatedDetour;
		std::vector<CServerDriver::_DetourFuncInfo<_DetourTrackedDeviceActivate_t>> CServerDriver::_deviceActivateDetours;
		std::map<vr::ITrackedDeviceServerDriver*, CServerDriver::_DetourFuncInfo<_DetourTrackedDeviceActivate_t>*> CServerDriver::_deviceActivateDetourMap; // _this => DetourInfo

		std::vector<CServerDriver::_DetourFuncInfo<_DetourTriggerHapticPulse_t>> CServerDriver::_deviceTriggerHapticPulseDetours;
		std::map<vr::IVRControllerComponent*, std::shared_ptr<OpenvrDeviceManipulationInfo>> CServerDriver::_controllerComponentToDeviceInfos; // ControllerComponent => ManipulationInfo



		CServerDriver::CServerDriver() {
			singleton = this;
			memset(m_openvrIdToVirtualDeviceMap, 0, sizeof(CTrackedDeviceDriver*) * vr::k_unMaxTrackedDeviceCount);
			memset(_openvrIdToDeviceInfoMap, 0, sizeof(OpenvrDeviceManipulationInfo*) * vr::k_unMaxTrackedDeviceCount);
		}


		CServerDriver::~CServerDriver() {
			LOG(TRACE) << "CServerDriver::~CServerDriver_InputEmulator()";
			// 10/10/2015 benj:  vrserver is exiting without calling Cleanup() to balance Init()
			// causing std::thread to call std::terminate

			// Apparently the above comment is not true anymore
			// Cleanup(); 
		}

		void CServerDriver::_poseUpatedDetourFunc(vr::IVRServerDriverHost* _this, uint32_t unWhichDevice, const vr::DriverPose_t& newPose, uint32_t unPoseStructSize) {
			// Call rates:
			//
			// Vive HMD: 1120 calls/s
			// Vive Controller: 369 calls/s each
			//
			// Time is key. If we assume 1 HMD and 13 controllers, we have a total of  ~6000 calls/s. That's about 166 microseconds per call at 100% load.

			/*if (unWhichDevice == 0) {
			#define CALLCOUNTSAMPESIZE 1000
			static uint64_t callcount = 0;
			static uint64_t starttime = 0;
			callcount++;
			if (starttime == 0) {
			starttime = std::chrono::duration_cast <std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
			} else if (callcount > CALLCOUNTSAMPESIZE) {
			auto endtime = std::chrono::duration_cast <std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
			auto diff = endtime - starttime;
			LOG(INFO) << "HMD pose update: " << CALLCOUNTSAMPESIZE << " calls in " << diff << "ms: " << CALLCOUNTSAMPESIZE * 1000 / diff << " calls / s";
			starttime = std::chrono::duration_cast <std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
			callcount = 0;
			}
			}*/
			if (true || (_openvrIdToDeviceInfoMap[unWhichDevice] && _openvrIdToDeviceInfoMap[unWhichDevice]->isValid())) {
				_openvrIdToDeviceInfoMap[unWhichDevice]->handleNewDevicePose(_this, _poseUpatedDetour.origFunc, unWhichDevice, newPose);
			}
			else {
				return _poseUpatedDetour.origFunc(_this, unWhichDevice, newPose, unPoseStructSize);
			}
		}

		void CServerDriver::_buttonPressedDetourFunc(vr::IVRServerDriverHost* _this, uint32_t unWhichDevice, vr::EVRButtonId eButtonId, double eventTimeOffset) {
			LOG(TRACE) << "Detour::buttonPressedDetourFunc(" << _this << ", " << unWhichDevice << ", " << (int)eButtonId << ", " << eventTimeOffset << ")";
			if (_openvrIdToDeviceInfoMap[unWhichDevice] && _openvrIdToDeviceInfoMap[unWhichDevice]->isValid()) {
				_openvrIdToDeviceInfoMap[unWhichDevice]->handleButtonEvent(_this, _buttonPressedDetour.origFunc, unWhichDevice, ButtonEventType::ButtonPressed, eButtonId, eventTimeOffset);
			}
			else {
				_buttonPressedDetour.origFunc(_this, unWhichDevice, eButtonId, eventTimeOffset);
			}
		}

		void CServerDriver::_buttonUnpressedDetourFunc(vr::IVRServerDriverHost* _this, uint32_t unWhichDevice, vr::EVRButtonId eButtonId, double eventTimeOffset) {
			LOG(TRACE) << "Detour::buttonUnpressedDetourFunc(" << _this << ", " << unWhichDevice << ", " << (int)eButtonId << ", " << eventTimeOffset << ")";
			if (_openvrIdToDeviceInfoMap[unWhichDevice] && _openvrIdToDeviceInfoMap[unWhichDevice]->isValid()) {
				_openvrIdToDeviceInfoMap[unWhichDevice]->handleButtonEvent(_this, _buttonUnpressedDetour.origFunc, unWhichDevice, ButtonEventType::ButtonUnpressed, eButtonId, eventTimeOffset);
			}
			else {
				_buttonUnpressedDetour.origFunc(_this, unWhichDevice, eButtonId, eventTimeOffset);
			}
		}

		void CServerDriver::_buttonTouchedDetourFunc(vr::IVRServerDriverHost* _this, uint32_t unWhichDevice, vr::EVRButtonId eButtonId, double eventTimeOffset) {
			LOG(TRACE) << "Detour::buttonTouchedDetourFunc(" << _this << ", " << unWhichDevice << ", " << (int)eButtonId << ", " << eventTimeOffset << ")";
			if (_openvrIdToDeviceInfoMap[unWhichDevice] && _openvrIdToDeviceInfoMap[unWhichDevice]->isValid()) {
				_openvrIdToDeviceInfoMap[unWhichDevice]->handleButtonEvent(_this, _buttonTouchedDetour.origFunc, unWhichDevice, ButtonEventType::ButtonTouched, eButtonId, eventTimeOffset);
			}
			else {
				_buttonTouchedDetour.origFunc(_this, unWhichDevice, eButtonId, eventTimeOffset);
			}
		}

		void CServerDriver::_buttonUntouchedDetourFunc(vr::IVRServerDriverHost* _this, uint32_t unWhichDevice, vr::EVRButtonId eButtonId, double eventTimeOffset) {
			LOG(TRACE) << "Detour::buttonUntouchedDetourFunc(" << _this << ", " << unWhichDevice << ", " << (int)eButtonId << ", " << eventTimeOffset << ")";
			if (_openvrIdToDeviceInfoMap[unWhichDevice] && _openvrIdToDeviceInfoMap[unWhichDevice]->isValid()) {
				_openvrIdToDeviceInfoMap[unWhichDevice]->handleButtonEvent(_this, _buttonUntouchedDetour.origFunc, unWhichDevice, ButtonEventType::ButtonUntouched, eButtonId, eventTimeOffset);
			}
			else {
				_buttonUntouchedDetour.origFunc(_this, unWhichDevice, eButtonId, eventTimeOffset);
			}
		}

		void CServerDriver::_axisUpdatedDetourFunc(vr::IVRServerDriverHost* _this, uint32_t unWhichDevice, uint32_t unWhichAxis, const vr::VRControllerAxis_t & axisState) {
			LOG(TRACE) << "Detour::axisUpdatedDetourFunc(" << _this << ", " << unWhichDevice << ", " << (int)unWhichAxis << ", <state>)";
			if (_openvrIdToDeviceInfoMap[unWhichDevice] && _openvrIdToDeviceInfoMap[unWhichDevice]->isValid()) {
				_openvrIdToDeviceInfoMap[unWhichDevice]->handleAxisEvent(_this, _axisUpdatedDetour.origFunc, unWhichDevice, unWhichAxis, axisState);
			}
			else {
				_axisUpdatedDetour.origFunc(_this, unWhichDevice, unWhichAxis, axisState);
			}
		}

		vr::EVRInitError CServerDriver::_deviceActivateDetourFunc(vr::ITrackedDeviceServerDriver* _this, uint32_t unObjectId) {
			LOG(TRACE) << "Detour::deviceActivateDetourFunc(" << _this << ", " << unObjectId << ")";
			auto i = _openvrDeviceInfos.find(_this);
			if (i != _openvrDeviceInfos.end()) {
				auto info = i->second;
				info->setOpenvrId(unObjectId);
				_openvrIdToDeviceInfoMap[unObjectId] = info.get();
				LOG(INFO) << "Detour::deviceActivateDetourFunc: sucessfully added to trackedDeviceInfos";
			}
			auto& d = _deviceActivateDetourMap.find(_this);
			if (d != _deviceActivateDetourMap.end()) {
				return d->second->origFunc(_this, unObjectId);
			}
			return vr::VRInitError_Unknown;
		}


		bool CServerDriver::_deviceTriggerHapticPulseDetourFunc(vr::IVRControllerComponent* _this, uint32_t unAxisId, uint16_t usPulseDurationMicroseconds) {
			LOG(TRACE) << "Detour::deviceTriggerHapticPulseDetourFunc(" << _this << ", " << unAxisId << ", " << usPulseDurationMicroseconds << ")";
			auto i = _controllerComponentToDeviceInfos.find(_this);
			if (i != _controllerComponentToDeviceInfos.end()) {
				auto info = i->second;
				return info->triggerHapticPulse(unAxisId, usPulseDurationMicroseconds);
			}
			return true;
		}


		bool CServerDriver::_deviceAddedDetourFunc(vr::IVRServerDriverHost* _this, const char *pchDeviceSerialNumber, vr::ETrackedDeviceClass eDeviceClass, vr::ITrackedDeviceServerDriver *pDriver) {
			LOG(TRACE) << "Detour::deviceAddedDetourFunc(" << _this << ", " << pchDeviceSerialNumber << ", " << (int)eDeviceClass << ", " << pDriver << ")";

			// Redirect activate() function
			auto deviceActivatedOrig = (*((void***)pDriver))[0];
			_DetourFuncInfo<_DetourTrackedDeviceActivate_t>* foundEntry = nullptr;
			for (auto& d : _deviceActivateDetours) {
				if (d.targetFunc == deviceActivatedOrig) {
					foundEntry = &d;
					break;
				}
			}
			if (!foundEntry) {
				_deviceActivateDetours.emplace_back();
				auto& d = _deviceActivateDetours[_deviceActivateDetours.size() - 1];
				MH_STATUS mhError;
				CREATE_MH_HOOK(d, _deviceActivateDetourFunc, "deviceActivateDetour", pDriver, 0);
				foundEntry = &d;
			}
			_deviceActivateDetourMap.insert({ pDriver, foundEntry });

			// Create ManipulationInfo entry
			auto info = std::make_shared<OpenvrDeviceManipulationInfo>(pDriver, eDeviceClass, vr::k_unTrackedDeviceIndexInvalid, _this);
			_openvrDeviceInfos.emplace(pDriver, info);

			// Redirect TriggerHapticPulse() function
			vr::IVRControllerComponent* controllerComponent = (vr::IVRControllerComponent*)pDriver->GetComponent(vr::IVRControllerComponent_Version);
			if (controllerComponent) {
				auto triggerHapticPulseOrig = (*((void***)controllerComponent))[1];
				_DetourFuncInfo<_DetourTriggerHapticPulse_t>* foundEntry2 = nullptr;
				for (auto& d : _deviceTriggerHapticPulseDetours) {
					if (d.targetFunc == triggerHapticPulseOrig) {
						foundEntry2 = &d;
						break;
					}
				}
				if (!foundEntry2) {
					_deviceTriggerHapticPulseDetours.emplace_back();
					auto& d = _deviceTriggerHapticPulseDetours[_deviceTriggerHapticPulseDetours.size() - 1];
					MH_STATUS mhError;
					CREATE_MH_HOOK(d, _deviceTriggerHapticPulseDetourFunc, "deviceTriggerHapticPulseDetour", controllerComponent, 1);
					foundEntry2 = &d;
				}
				info->setControllerComponent(controllerComponent, foundEntry2->origFunc);
				_controllerComponentToDeviceInfos.insert({ controllerComponent, info });
			}

			return _deviceAddedDetour.origFunc(_this, pchDeviceSerialNumber, eDeviceClass, pDriver);
		};


		vr::EVRInitError CServerDriver::Init(vr::IVRDriverContext *pDriverContext) {
			LOG(TRACE) << "CServerDriver::Init()";
			VR_INIT_SERVER_DRIVER_CONTEXT(pDriverContext);

			auto mhError = MH_Initialize();
			if (mhError == MH_OK) {
				CREATE_MH_HOOK(_deviceAddedDetour, _deviceAddedDetourFunc, "deviceAddedDetour", vr::VRServerDriverHost(), 0);
				CREATE_MH_HOOK(_poseUpatedDetour, _poseUpatedDetourFunc, "poseUpatedDetour", vr::VRServerDriverHost(), 1);
				CREATE_MH_HOOK(_buttonPressedDetour, _buttonPressedDetourFunc, "buttonPressedDetour", vr::VRServerDriverHost(), 3);
				CREATE_MH_HOOK(_buttonUnpressedDetour, _buttonUnpressedDetourFunc, "buttonUnpressedDetour", vr::VRServerDriverHost(), 4);
				CREATE_MH_HOOK(_buttonTouchedDetour, _buttonTouchedDetourFunc, "buttonTouchedDetour", vr::VRServerDriverHost(), 5);
				CREATE_MH_HOOK(_buttonUntouchedDetour, _buttonUntouchedDetourFunc, "buttonUntouchedDetour", vr::VRServerDriverHost(), 6);
				CREATE_MH_HOOK(_axisUpdatedDetour, _axisUpdatedDetourFunc, "axisUpdatedDetour", vr::VRServerDriverHost(), 7);
			}
			else {
				LOG(ERROR) << "Error while initialising minHook: " << MH_StatusToString(mhError);
			}

			// Start IPC thread
			shmCommunicator.init(this);
			return vr::VRInitError_None;
		}

		void CServerDriver::Cleanup() {
			LOG(TRACE) << "CServerDriver::Cleanup()";

			REMOVE_MH_HOOK(_deviceAddedDetour);
			REMOVE_MH_HOOK(_poseUpatedDetour);
			REMOVE_MH_HOOK(_buttonPressedDetour);
			REMOVE_MH_HOOK(_buttonUnpressedDetour);
			REMOVE_MH_HOOK(_buttonTouchedDetour);
			REMOVE_MH_HOOK(_buttonUntouchedDetour);
			REMOVE_MH_HOOK(_axisUpdatedDetour);

			MH_Uninitialize();
			shmCommunicator.shutdown();
			VR_CLEANUP_SERVER_DRIVER_CONTEXT();
		}

		// Call frequency: ~93Hz
		void CServerDriver::RunFrame() {
			/*#define CALLCOUNTSAMPESIZE 1000
			static uint64_t callcount = 0;
			static uint64_t starttime = 0;
			callcount++;
			if (starttime == 0) {
			starttime = std::chrono::duration_cast <std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
			} else if (callcount > CALLCOUNTSAMPESIZE) {
			auto endtime = std::chrono::duration_cast <std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
			auto diff = endtime - starttime;
			LOG(INFO) << "RunFrame: " << CALLCOUNTSAMPESIZE << " calls in " << diff << "ms: " << CALLCOUNTSAMPESIZE * 1000 / diff << " calls / s";
			starttime = std::chrono::duration_cast <std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
			callcount = 0;
			}*/
			for (int i = 0; i < vr::k_unMaxTrackedDeviceCount; ++i) {
				auto vd = m_virtualDevices[i];
				if (vd && vd->published() && vd->periodicPoseUpdates()) {
					vd->sendPoseUpdate();
				}
			}
		}

		int32_t CServerDriver::virtualDevices_addDevice(VirtualDeviceType type, const std::string& serial) {
			LOG(TRACE) << "CServerDriver::addTrackedDevice( " << serial << " )";
			std::lock_guard<std::recursive_mutex> lock(_virtualDevicesMutex);
			if (m_virtualDeviceCount >= vr::k_unMaxTrackedDeviceCount) {
				return -1;
			}
			for (uint32_t i = 0; i < m_virtualDeviceCount; ++i) {
				if (m_virtualDevices[i]->serialNumber().compare(serial) == 0) {
					return -2;
				}
			}
			uint32_t virtualDeviceId = 0;
			for (uint32_t i = 0; i < vr::k_unMaxTrackedDeviceCount; ++i) {
				if (!m_virtualDevices[i]) {
					virtualDeviceId = i;
					break;
				}
			}
			switch (type) {
			case VirtualDeviceType::TrackedController: {
				m_virtualDevices[virtualDeviceId] = std::make_shared<CTrackedControllerDriver>(this, serial);
				LOG(INFO) << "Added new tracked controller:  type " << (int)type << ", serial \"" << serial << "\", emulatedDeviceId " << virtualDeviceId;
				m_virtualDeviceCount++;
				return virtualDeviceId;
			}
			default:
				return -3;
			}
		}

		int32_t CServerDriver::virtualDevices_publishDevice(uint32_t emulatedDeviceId, bool notify) {
			LOG(TRACE) << "CServerDriver::publishTrackedDevice( " << emulatedDeviceId << " )";
			std::lock_guard<std::recursive_mutex> lock(_virtualDevicesMutex);
			if (emulatedDeviceId >= m_virtualDeviceCount) {
				return -1;
			}
			else if (!m_virtualDevices[emulatedDeviceId]) {
				return -2;
			}
			else {
				auto device = m_virtualDevices[emulatedDeviceId].get();
				if (device->published()) {
					return -3;
				}
				try {
					device->publish();
					LOG(INFO) << "Published tracked controller: virtualDeviceId " << emulatedDeviceId;
				}
				catch (std::exception& e) {
					LOG(ERROR) << "Error while publishing controller " << emulatedDeviceId << ": " << e.what();
					return -4;
				}
				return 0;
			}
		}

		void CServerDriver::_trackedDeviceActivated(uint32_t deviceId, CTrackedDeviceDriver * device) {
			m_openvrIdToVirtualDeviceMap[deviceId] = device;
		}

		void CServerDriver::_trackedDeviceDeactivated(uint32_t deviceId) {
			m_openvrIdToVirtualDeviceMap[deviceId] = nullptr;
		}

		void CServerDriver::openvr_buttonEvent(uint32_t unWhichDevice, ButtonEventType eventType, vr::EVRButtonId eButtonId, double eventTimeOffset) {
			auto devicePtr = this->m_openvrIdToVirtualDeviceMap[unWhichDevice];
			if (devicePtr && devicePtr->deviceType() == VirtualDeviceType::TrackedController) {
				((CTrackedControllerDriver*)devicePtr)->buttonEvent(eventType, eButtonId, eventTimeOffset);
			}
			else {
				vr::IVRServerDriverHost* driverHost;
				if (_openvrIdToDeviceInfoMap[unWhichDevice] && _openvrIdToDeviceInfoMap[unWhichDevice]->isValid()) {
					driverHost = _openvrIdToDeviceInfoMap[unWhichDevice]->driverHost();
				}
				else {
					driverHost = vr::VRServerDriverHost();
				}
				switch (eventType) {
				case ButtonEventType::ButtonPressed:
					driverHost->TrackedDeviceButtonPressed(unWhichDevice, eButtonId, eventTimeOffset);
					break;
				case ButtonEventType::ButtonUnpressed:
					driverHost->TrackedDeviceButtonUnpressed(unWhichDevice, eButtonId, eventTimeOffset);
					break;
				case ButtonEventType::ButtonTouched:
					driverHost->TrackedDeviceButtonTouched(unWhichDevice, eButtonId, eventTimeOffset);
					break;
				case ButtonEventType::ButtonUntouched:
					driverHost->TrackedDeviceButtonUntouched(unWhichDevice, eButtonId, eventTimeOffset);
					break;
				default: {
					std::stringstream ss;
					ss << "Unknown button event type (" << (int)eventType << ")";
					throw std::runtime_error(ss.str());
				}
				}
			}
		}

		void CServerDriver::openvr_axisEvent(uint32_t unWhichDevice, uint32_t unWhichAxis, const vr::VRControllerAxis_t & axisState) {
			/*auto devicePtr = this->m_openvrIdToVirtualDeviceMap[unWhichDevice];
			if (devicePtr && devicePtr->deviceType() == VirtualDeviceType::TrackedController) {
				((CTrackedControllerDriver*)devicePtr)->axisEvent(unWhichAxis, axisState);
			}
			else {
				vr::IVRServerDriverHost* driverHost;
				if (_openvrIdToDeviceInfoMap[unWhichDevice] && _openvrIdToDeviceInfoMap[unWhichDevice]->isValid()) {
					driverHost = _openvrIdToDeviceInfoMap[unWhichDevice]->driverHost();
				}
				else {
					driverHost = vr::VRServerDriverHost();
				}
				driverHost->TrackedDeviceAxisUpdated(unWhichDevice, unWhichAxis, axisState);
			}*/
		}

		void CServerDriver::openvr_poseUpdate(uint32_t unWhichDevice, vr::DriverPose_t & newPose, int64_t timestamp) {
			auto devicePtr = this->m_openvrIdToVirtualDeviceMap[unWhichDevice];
			auto now = std::chrono::duration_cast <std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
			auto diff = 0.0;
			if (timestamp < now) {
				diff = ((double)now - timestamp) / 1000.0;
			}
			if (devicePtr) {
				devicePtr->updatePose(newPose, -diff);
			}
			else {
				vr::IVRServerDriverHost* driverHost;
				if (_openvrIdToDeviceInfoMap[unWhichDevice] && _openvrIdToDeviceInfoMap[unWhichDevice]->isValid()) {
					driverHost = _openvrIdToDeviceInfoMap[unWhichDevice]->driverHost();
				}
				else {
					driverHost = vr::VRServerDriverHost();
				}
				newPose.poseTimeOffset -= diff;
				driverHost->TrackedDevicePoseUpdated(unWhichDevice, newPose, sizeof(vr::DriverPose_t));
			}
		}

		void CServerDriver::openvr_proximityEvent(uint32_t unWhichDevice, bool bProximitySensorTriggered) {
			vr::IVRServerDriverHost* driverHost;
			if (_openvrIdToDeviceInfoMap[unWhichDevice] && _openvrIdToDeviceInfoMap[unWhichDevice]->isValid()) {
				driverHost = _openvrIdToDeviceInfoMap[unWhichDevice]->driverHost();
			}
			else {
				driverHost = vr::VRServerDriverHost();
			}
			driverHost->ProximitySensorState(unWhichDevice, bProximitySensorTriggered);
		}

		void CServerDriver::openvr_vendorSpecificEvent(uint32_t unWhichDevice, vr::EVREventType eventType, vr::VREvent_Data_t & eventData, double eventTimeOffset) {
			vr::IVRServerDriverHost* driverHost;
			if (_openvrIdToDeviceInfoMap[unWhichDevice] && _openvrIdToDeviceInfoMap[unWhichDevice]->isValid()) {
				driverHost = _openvrIdToDeviceInfoMap[unWhichDevice]->driverHost();
			}
			else {
				driverHost = vr::VRServerDriverHost();
			}
			driverHost->VendorSpecificEvent(unWhichDevice, eventType, eventData, eventTimeOffset);
		}

		uint32_t CServerDriver::virtualDevices_getDeviceCount() {
			std::lock_guard<std::recursive_mutex> lock(this->_virtualDevicesMutex);
			return this->m_virtualDeviceCount;
		}

		CTrackedDeviceDriver* CServerDriver::virtualDevices_getDevice(uint32_t unWhichDevice) {
			std::lock_guard<std::recursive_mutex> lock(this->_virtualDevicesMutex);
			if (this->m_virtualDevices[unWhichDevice]) {
				return this->m_virtualDevices[unWhichDevice].get();
			}
			return nullptr;
		}

		CTrackedDeviceDriver * CServerDriver::virtualDevices_findDevice(const std::string& serial) {
			for (uint32_t i = 0; i < this->m_virtualDeviceCount; ++i) {
				if (this->m_virtualDevices[i]->serialNumber().compare(serial) == 0) {
					return this->m_virtualDevices[i].get();
				}
			}
			return nullptr;
		}

		OpenvrDeviceManipulationInfo* CServerDriver::deviceManipulation_getInfo(uint32_t unWhichDevice) {
			std::lock_guard<std::recursive_mutex> lock(_openvrDevicesMutex);
			if (_openvrIdToDeviceInfoMap[unWhichDevice] && _openvrIdToDeviceInfoMap[unWhichDevice]->isValid()) {
				return _openvrIdToDeviceInfoMap[unWhichDevice];
			}
			return nullptr;
		}

		void CServerDriver::enableStepDetection(bool enable) {
			_stepPoseDetectEnabled = enable;
			g_stepPoseDetectEnabled = enable;
			for (auto d : _openvrDeviceInfos) {
				if (enable) {
					d.second->setStepDetectionMode();
				}
				else {
					d.second->setDefaultMode();
				}
			}
		}

		void CServerDriver::setStepIntSec(float value) {
			_stepIntegrateStepLimit = (double)value;
		}

		void CServerDriver::setHMDThreshold(vr::HmdVector3d_t value) {
			_hmdThreshold = value;
		}

		void CServerDriver::setHandJogThreshold(float value) {
			_handJogThreshold = value;
		}

		void CServerDriver::setHandRunThreshold(float value) {
			_handJogThreshold = value;
		}

		void CServerDriver::setStepPoseDetected(bool enable) {
			_stepPoseDetected = enable;
		}

		bool CServerDriver::isStepDetectionEnabled() {
			return this->_stepPoseDetectEnabled;
		}

		bool CServerDriver::_applyStepPoseDetect(vr::DriverPose_t& pose, OpenvrDeviceManipulationInfo* deviceInfo, vr::HmdQuaternion_t stepUpDir) {
			if (this->_stepPoseDetectEnabled && pose.poseIsValid) {
				double deltatime = 1.0 / 40.0;
				auto now = std::chrono::duration_cast <std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
				double tdiff = (double)(now - _timeLastStepTaken);
				if (!this->_stepPoseDetected) {
					/*vr::HmdQuaternion_t tmpConj = vrmath::quaternionConjugate(pose.qWorldFromDriverRotation);
					auto poseWorldRot = tmpConj * pose.qRotation;
					auto poseWorldVelo = vrmath::quaternionRotateVector(tmpConj, pose.vecVelocity, false);
					double xAcc = poseWorldVelo.v[0];
					double yAcc = poseWorldVelo.v[1];
					double zAcc = poseWorldVelo.v[2];*/
					//auto poseWorldRot = pose.qRotation;
					//double xAcc = pose.vecVelocity[0];
					//double yAcc = pose.vecVelocity[1];
					//double zAcc = pose.vecVelocity[2];

					/*double zsqr = poseWorldRot.z * poseWorldRot.z;

					// roll (x-axis rotation)
					double t0 = +2.0 * (poseWorldRot.w * poseWorldRot.x + poseWorldRot.y * poseWorldRot.z);
					double t1 = +1.0 - 2.0 * (zsqr + poseWorldRot.x * poseWorldRot.x);
					double roll = std::atan2(t0, t1);

					// pitch (z-axis rotation)
					double t2 = +2.0 * (poseWorldRot.w * poseWorldRot.z + poseWorldRot.y * poseWorldRot.x);
					t2 = t2 > 1.0 ? 1.0 : t2;
					t2 = t2 < -1.0 ? -1.0 : t2;
					double pitch = std::asin(t2);

					// yaw (y-axis rotation)
					double t3 = +2.0 * (poseWorldRot.w * poseWorldRot.y + poseWorldRot.x * poseWorldRot.z);
					double t4 = +1.0 - 2.0 * (zsqr + poseWorldRot.y * poseWorldRot.y);
					double yaw = std::atan2(t3, t4);
					*/
					switch (deviceInfo->deviceClass()) {
					case vr::ETrackedDeviceClass::TrackedDeviceClass_HMD:
						//LOG(INFO) << "HMD Step: x " << pose.vecVelocity[0] << " y " << pose.vecVelocity[1] << " z " << pose.vecVelocity[2];// << " roll " << roll << " yaw " << yaw << " pitch " << pitch;
						if ( isTakingStep( pose.vecVelocity, _hmdThreshold) ) {
							_openvrDeviceStepPoseTracker[0] = -1;
							_openvrDeviceStepPoseTracker[1] = -1;
							_openvrDeviceStepPoseTracker[2] = -1;
						}
						else {
							_openvrDeviceStepPoseTracker[0] = 0;
							_openvrDeviceStepPoseTracker[1] = 0;
							_openvrDeviceStepPoseTracker[2] = 0;
						}
						break;
					case vr::ETrackedDeviceClass::TrackedDeviceClass_Controller:
						//LOG(INFO) << "CONTROLLER MAG VEL: " << pose.vecVelocity[0] + pose.vecVelocity[1] + pose.vecVelocity[2];
						break;
					default:
						break;
					}
					if (_openvrDeviceStepPoseTracker[0] != 0) {
						//&& _openvrDeviceStepPoseTracker[1] != 0 && _openvrDeviceStepPoseTracker[2] != 0 ) {
						//&& (_openvrDeviceStepPoseTracker[1] != _openvrDeviceStepPoseTracker[2])) {
						this->setStepPoseDetected(true);
						_openvrDeviceStepPoseTracker[1] = -1;
						_openvrDeviceStepPoseTracker[2] = -1;
					}
				}
				if (this->_stepPoseDetected
					&& _openvrDeviceStepPoseTracker[0] == 0
					&& (deviceInfo->openvrId() == _openvrDeviceStepPoseTracker[1]
						|| deviceInfo->openvrId() == _openvrDeviceStepPoseTracker[2])) {
					vr::IVRServerDriverHost* driverHost;
					int deviceId = deviceInfo->openvrId();
					if (_openvrIdToDeviceInfoMap[deviceId] && _openvrIdToDeviceInfoMap[deviceId]->isValid()) {
						driverHost = _openvrIdToDeviceInfoMap[deviceId]->driverHost();
					}
					else {
						driverHost = vr::VRServerDriverHost();
					}
					driverHost->TrackedDeviceButtonTouched(deviceId, vr::k_EButton_Axis0, 0);
					vr::VRControllerAxis_t axisState;
					axisState.x = 0;
					axisState.y = 0.5;
					if (isJoggingStep(pose.vecVelocity)) {
						axisState.y = 1.0;
					}
					driverHost->TrackedDeviceAxisUpdated(deviceId, 0, axisState);
					this->_hasUnTouchedStepAxis = 0;
					if (isRunningStep(pose.vecVelocity)) {
						driverHost->TrackedDeviceButtonPressed(deviceId, vr::EVRButtonId::k_EButton_Axis0, 0);
					}
				}
				else {
					if (this->_hasUnTouchedStepAxis < 50) {
						vr::VRControllerAxis_t axisState;
						axisState.x = 0;
						axisState.y = 0;
						vr::IVRServerDriverHost* driverHost;
						int deviceId = deviceInfo->openvrId();
						if (_openvrIdToDeviceInfoMap[deviceId] && _openvrIdToDeviceInfoMap[deviceId]->isValid()) {
							driverHost = _openvrIdToDeviceInfoMap[deviceId]->driverHost();
						}
						else {
							driverHost = vr::VRServerDriverHost();
						}
						driverHost->TrackedDeviceButtonUnpressed(deviceId, vr::EVRButtonId::k_EButton_Axis0, 0);
						driverHost->TrackedDeviceAxisUpdated(deviceId, 0, axisState);
						driverHost->TrackedDeviceButtonUntouched(deviceId, vr::k_EButton_Axis0, 0);
						this->_hasUnTouchedStepAxis++;
					}
				}
				if (this->_stepPoseDetected) {
					if (_stepIntegrateSteps >= 0.07) { //_stepIntegrateStepLimit) {
						this->setStepPoseDetected(false);
						_stepIntegrateSteps = 0.0;
						_handsPointDir = { 0.0, 0.0, 0.0 };
						_timeLastStepTaken = now;
						_openvrDeviceStepPoseTracker[0] = 0;
						_openvrDeviceStepPoseTracker[1] = 0;
						_openvrDeviceStepPoseTracker[2] = 0;
					}
					else {
						switch (deviceInfo->deviceClass()) {
						case vr::ETrackedDeviceClass::TrackedDeviceClass_Controller:
							if (_openvrDeviceStepPoseTracker[0] != 0) {
								if (_openvrDeviceStepPoseTracker[1] < 0) {
									_openvrDeviceStepPoseTracker[1] = deviceInfo->openvrId();
								}
								else if (_openvrDeviceStepPoseTracker[2] < 0) {
									_openvrDeviceStepPoseTracker[2] = deviceInfo->openvrId();
									_openvrDeviceStepPoseTracker[0] = 0;
								}
							}
							else if (isJoggingStep(pose.vecVelocity) || isRunningStep(pose.vecVelocity)) {
								_stepIntegrateSteps = 0;
							}
							break;
						case vr::ETrackedDeviceClass::TrackedDeviceClass_HMD:
							if ( isTakingStep(pose.vecVelocity, _hmdThreshold) ) {
								_stepIntegrateSteps = 0;
							}
							break;
						default:
							break;
						}
					}
					double tdiff = (double)(now - _timeLastTick);
					_stepIntegrateSteps += tdiff;
				}
				_timeLastTick = now;
			}

			return false;
		}

		bool CServerDriver::isTakingStep(double * vel, vr::HmdVector3d_t threshold) {
			return ((std::abs(vel[2]) < threshold.v[2]) &&
				(std::abs(vel[0]) < threshold.v[0]) &&
				((vel[1] > threshold.v[1] && (vel[1] > vel[0] && vel[1] > vel[2]))
					|| (vel[1] < -1 * threshold.v[1] && (vel[1] < vel[0] && vel[1] < vel[2]))));
		}

		bool CServerDriver::isJoggingStep(double * vel) {
			float magVel = (std::abs(vel[0]) + std::abs(vel[1]) + std::abs(vel[2]));
			return ( magVel > _handJogThreshold);
		}


		bool CServerDriver::isRunningStep(double * vel) {
			float magVel = (std::abs(vel[0]) + std::abs(vel[1]) + std::abs(vel[2]));
			return (magVel > _handRunThreshold);
		}

	} // end namespace driver
} // end namespace vrwalkinplace

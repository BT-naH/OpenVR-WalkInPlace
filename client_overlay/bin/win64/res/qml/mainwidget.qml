import QtQuick 2.7
import QtQuick.Controls 2.0
import QtQuick.Layouts 1.0


Rectangle {
    id: root
    color: "#1b2939"
    width: 1200
    height: 800

    property DeviceManipulationPage devicePage: DeviceManipulationPage {
        stackView: mainView
    }

    property DeviceOffsetsPage deviceOffsetsPage:  DeviceOffsetsPage {
        stackView: mainView
    }

    property StepDetectionPage stepDetectionPage:  StepDetectionPage {
        stackView: mainView
    }

    property MotionCompensationPage motionCompensationPage:  MotionCompensationPage {
        stackView: mainView
    }

    StackView {
        id: mainView
        anchors.fill: parent

		pushEnter: Transition {
			PropertyAnimation {
				property: "x"
				from: mainView.width
				to: 0
				duration: 200
			}
		}
		pushExit: Transition {
			PropertyAnimation {
				property: "x"
				from: 0
				to: -mainView.width
				duration: 200
			}
		}
		popEnter: Transition {
			PropertyAnimation {
				property: "x"
				from: -mainView.width
				to: 0
				duration: 200
			}
		}
		popExit: Transition {
			PropertyAnimation {
				property: "x"
				from: 0
				to: mainView.width
				duration: 200
			}
		}

        initialItem: stepDetectionPage
    }
}

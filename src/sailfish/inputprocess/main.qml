import QtQuick 2.0
import Sailfish.Silica 1.0
import Thing 1.0

// TODO: Move to client/qml/sailfish_inputprocess.qml or something installable like that

ApplicationWindow {
	initialPage: Page {
		allowedOrientations: Orientation.Landscape
		Column {
			spacing: Theme.paddingSmall
			anchors.centerIn: parent
			TextField {
				text: userInput
				focus: true
				EnterKey.enabled: true
				EnterKey.onClicked: {
					console.log("enter: \""+text+"\"")
					Thing.acceptInput(text)
				}
			}
			Button {
				text: "Done"
				onClicked: {
					console.log("button: \""+text+"\"")
					Thing.acceptInput(text)
				}
			}
		}
	}
}


import QtQuick 2.5
import QtMultimedia 5.4
import QtSensors 5.0
import QtPositioning 5.2
import Sailfish.Silica 1.0
import Nemo.KeepAlive 1.2
import harbour.periscope 1.0

Page {
    id: page

    allowedOrientations: Orientation.Landscape | Orientation.LandscapeInverted
    backNavigation: !page.recording && !page.captureInProgress
    backgroundColor: "black"

    readonly property bool recorderPage: true

    property bool recording: false
    property bool captureInProgress: false
    property bool focusAttemptActive: false
    property bool pendingStop: false
    property bool resolutionApplied: false
    property bool lastPhotoValid: true
    property int captureWidth: 0
    property int captureHeight: 0
    property int pendingRequestId: -1
    property int sessionCount: 0
    property string pendingPath
    property bool pendingGpsExpected: false
    property string statusText: qsTr("Preparing camera…")
    property string errorText
    property string lastPhotoPath
    property string sessionSummary
    property string sessionPath
    property double clockTick: Date.now()
    property double lastCaptureTime: 0

    readonly property var currentPosition: positionSource.position
    readonly property int orientationReading: orientationSensor.reading ? orientationSensor.reading.orientation : -1
    readonly property bool landscape: orientationReading === OrientationReading.LeftUp || orientationReading === OrientationReading.RightUp
    readonly property int deviceRotation: {
        switch (orientationReading) {
        case OrientationReading.TopUp:
            return 0;
        case OrientationReading.RightUp:
            return 90;
        case OrientationReading.TopDown:
            return 180;
        case OrientationReading.LeftUp:
            return 270;
        default:
            return 0;
        }
    }
    readonly property int captureOrientation: (720 + cameraObject.orientation + deviceRotation) % 360
    // The gst video sink already compensates for the sensor mount angle using the
    // orientation tag on each viewfinder buffer, so only the page rotation needs
    // to be cancelled out here. VideoOutput.orientation is anti-clockwise.
    readonly property int pageRotation: {
        switch (page.orientation) {
        case Orientation.Landscape:
            return 90;
        case Orientation.PortraitInverted:
            return 180;
        case Orientation.LandscapeInverted:
            return 270;
        default:
            return 0;
        }
    }
    readonly property bool gpsReady: {
        if (!currentPosition || !currentPosition.coordinate || !currentPosition.coordinate.isValid)
            return false;
        var timestamp = currentPosition.timestamp;
        if (!timestamp || isNaN(timestamp.getTime()))
            return false;
        var age = (page.clockTick - timestamp.getTime()) / 1000;
        return age >= 0 && age <= 10;
    }
    readonly property real maxGpsAccuracy: 10
    readonly property bool gpsAccuracyReady: {
        if (!gpsReady)
            return false;
        // Without a reported accuracy there is nothing to reject the fix for.
        if (!currentPosition.horizontalAccuracyValid)
            return true;
        return currentPosition.horizontalAccuracy <= maxGpsAccuracy;
    }
    readonly property string gpsText: {
        if (!gpsReady)
            return qsTr("GPS: waiting for position");
        var coordinate = currentPosition.coordinate;
        var latitude = coordinate.latitude.toFixed(5);
        var longitude = coordinate.longitude.toFixed(5);
        if (currentPosition.horizontalAccuracyValid) {
            return qsTr("GPS: %1, %2 (±%3 m)").arg(latitude).arg(longitude).arg(Math.round(currentPosition.horizontalAccuracy));
        }
        return qsTr("GPS: %1, %2").arg(latitude).arg(longitude);
    }
    readonly property bool compassReady: compass.reading !== null && compass.reading !== undefined && !isNaN(compass.reading.azimuth)
    readonly property real cameraHeading: {
        if (!compassReady)
            return -1;
        var heading = compass.reading.azimuth;
        if (orientationReading === OrientationReading.LeftUp)
            heading = (heading + 270) % 360;
        else if (orientationReading === OrientationReading.RightUp)
            heading = (heading + 90) % 360;
        return heading;
    }
    readonly property bool cameraAvailable: cameraObject.availability === Camera.Available && cameraObject.errorCode === Camera.NoError
    readonly property string resolutionText: resolutionApplied ? qsTr("%1×%2").arg(captureWidth).arg(captureHeight) : qsTr("Preparing camera…")

    function pick16x9(resolutions) {
        var best = null;
        var bestPixels = 0;
        for (var i = 0; i < resolutions.length; ++i) {
            var resolution = resolutions[i];
            var ratio = resolution.width / resolution.height;
            var pixels = resolution.width * resolution.height;
            if (Math.abs(ratio - 16.0 / 9.0) < 0.05 && pixels > bestPixels) {
                best = resolution;
                bestPixels = pixels;
            }
        }
        return best;
    }

    function applyResolutions() {
        if (resolutionApplied)
            return;
        var imageResolution = pick16x9(capabilities.imageResolutions);
        if (!imageResolution)
            imageResolution = {
                "width": 1920,
                "height": 1080
            };

        cameraObject.imageCapture.resolution = Qt.size(imageResolution.width, imageResolution.height);
        captureWidth = imageResolution.width;
        captureHeight = imageResolution.height;

        var viewfinderResolution = pick16x9(capabilities.viewfinderResolutions);
        if (viewfinderResolution) {
            cameraObject.viewfinder.resolution = Qt.size(viewfinderResolution.width, viewfinderResolution.height);
        }

        resolutionApplied = true;
        updateStatus();
    }

    function updateStatus() {
        if (!cameraAvailable) {
            statusText = qsTr("Camera unavailable");
            return;
        }
        if (!resolutionApplied) {
            statusText = qsTr("Preparing camera…");
            return;
        }
        if (recording) {
            if (!landscape)
                statusText = qsTr("Hold the phone in landscape to record!");
            else if (!gpsAccuracyReady)
                statusText = qsTr("Waiting for GPS accuracy of %1 m or better…").arg(maxGpsAccuracy);
            else if (focusAttemptActive)
                statusText = qsTr("Focusing…");
            else if (captureInProgress)
                statusText = qsTr("Saving photo…");
            else
                statusText = "";
        } else {
            statusText = "";
        }
    }

    function startRecording() {
        if (recording || captureInProgress || pendingStop || !cameraAvailable || !resolutionApplied)
            return;
        errorText = "";
        sessionSummary = "";
        lastPhotoPath = "";
        lastPhotoValid = true;
        captureInProgress = false;
        focusAttemptActive = false;
        focusWatchdog.stop();
        pendingStop = false;
        pendingRequestId = -1;
        pendingPath = "";

        sessionPath = recorder.startSession();
        if (sessionPath.length === 0) {
            errorText = recorder.lastError;
            updateStatus();
            return;
        }

        sessionCount = 0;
        recording = true;
        lastCaptureTime = Date.now();
        updateStatus();
        captureTimer.interval = 0;
        captureTimer.start();
    }

    function stopRecording() {
        if (!recording)
            return;
        recording = false;
        captureTimer.stop();
        focusAttemptActive = false;
        focusWatchdog.stop();
        if (!captureInProgress)
            cameraObject.unlock();

        if (captureInProgress) {
            pendingStop = true;
            stopTimeout.start();
            updateStatus();
        } else {
            finalizeSession();
        }
    }

    function finalizeSession() {
        pendingStop = false;
        captureInProgress = false;
        pendingRequestId = -1;
        pendingPath = "";
        stopTimeout.stop();

        var result = recorder.finishSession();
        if (result.path)
            sessionPath = result.path;
        sessionCount = result.count ? result.count : 0;

        if (sessionCount === 0)
            sessionSummary = qsTr("No photos recorded");
        else
            sessionSummary = qsTr("%1 photos saved in %2").arg(sessionCount).arg(sessionPath);

        updateStatus();
    }

    function scheduleNext() {
        if (!recording)
            return;
        var interval = Math.max(1, recorder.intervalSeconds) * 1000;
        var elapsed = Date.now() - lastCaptureTime;
        captureTimer.interval = Math.max(1, interval - elapsed);
        captureTimer.start();
    }

    function cancelFocus() {
        focusAttemptActive = false;
        focusWatchdog.stop();
        cameraObject.unlock();
    }

    function tryCapture() {
        if (!recording || focusAttemptActive)
            return;
        if (captureInProgress) {
            updateStatus();
            return;
        }
        if (!landscape) {
            updateStatus();
            scheduleNext();
            return;
        }
        if (!gpsReady) {
            updateStatus();
            scheduleNext();
            return;
        }
        if (!gpsAccuracyReady) {
            updateStatus();
            scheduleNext();
            return;
        }
        if (!cameraObject.imageCapture.ready) {
            updateStatus();
            scheduleNext();
            return;
        }

        // Only take the picture once the autofocus has settled. A picture
        // taken while the camera is still focusing is usually blurred.
        if (cameraObject.lockStatus === Camera.Locked) {
            capturePhoto();
            return;
        }

        focusAttemptActive = true;
        focusWatchdog.start();
        if (cameraObject.lockStatus !== Camera.Searching)
            cameraObject.searchAndLock();
        updateStatus();
    }

    function capturePhoto() {
        if (!gpsAccuracyReady) {
            updateStatus();
            scheduleNext();
            return;
        }

        var path = recorder.nextPhotoPath();
        if (path.length === 0) {
            errorText = recorder.lastError;
            updateStatus();
            scheduleNext();
            return;
        }

        writeMetadata();
        pendingPath = path;
        pendingRequestId = cameraObject.imageCapture.captureToLocation(path);
        if (pendingRequestId < 0) {
            captureInProgress = false;
            errorText = cameraObject.imageCapture.errorString;
            pendingPath = "";
            updateStatus();
            scheduleNext();
            return;
        }

        captureInProgress = true;
        lastCaptureTime = Date.now();
        updateStatus();
    }

    function writeMetadata() {
        cameraObject.metaData.date = new Date();

        pendingGpsExpected = gpsReady;
        if (gpsReady) {
            var coordinate = currentPosition.coordinate;
            cameraObject.metaData.gpsLatitude = coordinate.latitude;
            cameraObject.metaData.gpsLongitude = coordinate.longitude;
            cameraObject.metaData.gpsAltitude = currentPosition.altitudeValid ? coordinate.altitude : undefined;
        } else {
            cameraObject.metaData.gpsLatitude = undefined;
            cameraObject.metaData.gpsLongitude = undefined;
            cameraObject.metaData.gpsAltitude = undefined;
        }

        cameraObject.metaData.gpsImgDirection = compassReady ? cameraHeading : undefined;
    }

    function onImageSaved(requestId, path) {
        if (requestId !== pendingRequestId)
            return;
        captureInProgress = false;
        pendingRequestId = -1;
        pendingPath = "";
        cameraObject.unlock();

        recorder.photoSaved(path);
        sessionCount = recorder.photoCount;
        lastPhotoPath = path;

        if (pendingStop || !recording) {
            lastPhotoValid = true;
            finalizeSession();
            return;
        }

        // Keep the capture cadence independent from the metadata scan.
        scheduleNext();

        var meta = metadata.read(path);
        lastPhotoValid = meta.valid && meta.jpeg && meta.capturedAt && (!pendingGpsExpected || meta.hasGps);
        if (!lastPhotoValid)
            errorText = qsTr("The saved picture is missing GPS or date metadata");
        else
            errorText = "";

        updateStatus();
    }

    function onCaptureFailed(requestId, message) {
        if (requestId !== pendingRequestId)
            return;
        captureInProgress = false;
        pendingRequestId = -1;
        pendingPath = "";
        cameraObject.unlock();
        errorText = message;
        updateStatus();

        if (pendingStop || !recording) {
            finalizeSession();
            return;
        }

        scheduleNext();
    }

    function startCamera() {
        if (cameraObject.cameraState !== Camera.ActiveState)
            cameraObject.cameraState = Camera.ActiveState;
    }

    function stopCamera() {
        if (cameraObject.cameraState !== Camera.UnloadedState)
            cameraObject.cameraState = Camera.UnloadedState;
    }

    onStatusChanged: {
        if (status === PageStatus.Active) {
            positionSource.active = true;
            compass.active = true;
            orientationSensor.active = true;
            startCamera();
        } else if (status === PageStatus.Deactivating) {
            stopRecording();
            stopCamera();
            positionSource.active = false;
            compass.active = false;
            orientationSensor.active = false;
        }
    }

    Component.onCompleted: updateStatus()

    Connections {
        target: Qt.application

        onStateChanged: {
            if (Qt.application.state !== Qt.ApplicationActive && page.recording)
                page.stopRecording();
        }
    }

    Connections {
        target: capabilities

        onReadyChanged: {
            if (capabilities.ready)
                page.applyResolutions();
        }
    }

    Connections {
        target: orientationSensor
        onReadingChanged: page.updateStatus()
    }

    Connections {
        target: positionSource
        onPositionChanged: page.updateStatus()
    }

    Connections {
        target: cameraObject

        onLockStatusChanged: {
            if (!page.recording || !page.focusAttemptActive || page.captureInProgress)
                return;
            if (cameraObject.lockStatus === Camera.Locked) {
                page.focusAttemptActive = false;
                focusWatchdog.stop();
                page.capturePhoto();
            } else if (cameraObject.lockStatus === Camera.Unlocked) {
                // The autofocus failed or was lost: skip this picture
                // instead of saving one that is likely out of focus.
                page.cancelFocus();
                page.updateStatus();
                page.scheduleNext();
            }
        }
    }

    VideoOutput {
        anchors.fill: parent
        source: cameraObject
        fillMode: VideoOutput.PreserveAspectCrop
        orientation: page.pageRotation
    }

    Rectangle {
        anchors {
            top: parent.top
            left: parent.left
            right: parent.right
        }
        height: topContent.height + Theme.paddingLarge
        gradient: Gradient {
            GradientStop {
                position: 0.0
                color: "#cc000000"
            }
            GradientStop {
                position: 1.0
                color: "#00000000"
            }
        }
    }

    Column {
        id: topContent
        anchors {
            top: parent.top
            left: parent.left
            right: parent.right
            topMargin: Theme.paddingLarge
        }
        spacing: Theme.paddingSmall

        Label {
            x: Theme.horizontalPageMargin
            width: parent.width - 2 * Theme.horizontalPageMargin
            text: page.gpsText
            font.pixelSize: Theme.fontSizeExtraSmall
            color: page.gpsReady ? Theme.secondaryColor : Theme.highlightColor
            elide: Text.ElideRight
        }

        Row {
            x: Theme.horizontalPageMargin
            width: parent.width - 2 * Theme.horizontalPageMargin
            spacing: Theme.paddingMedium

            Label {
                width: parent.width - statusRight.width - parent.spacing
                text: page.statusText
                font.pixelSize: Theme.fontSizeSmall
                color: page.recording && !page.landscape ? Theme.errorColor : (page.recording ? Theme.highlightColor : Theme.secondaryColor)
                elide: Text.ElideRight
            }

            Label {
                id: statusRight
                text: page.resolutionText
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryColor
            }
        }
    }

    Rectangle {
        anchors {
            bottom: parent.bottom
            left: parent.left
            right: parent.right
        }
        height: bottomContent.height + Theme.paddingLarge
        gradient: Gradient {
            GradientStop {
                position: 0.0
                color: "#00000000"
            }
            GradientStop {
                position: 1.0
                color: "#cc000000"
            }
        }
    }

    Column {
        id: bottomContent
        anchors {
            bottom: parent.bottom
            left: parent.left
            right: parent.right
            margins: Theme.horizontalPageMargin
        }
        spacing: Theme.paddingMedium

        Image {
            visible: page.lastPhotoPath.length > 0
            anchors.horizontalCenter: parent.horizontalCenter
            width: Theme.iconSizeLarge
            height: width
            asynchronous: true
            autoTransform: true
            fillMode: Image.PreserveAspectCrop
            sourceSize.width: width
            sourceSize.height: height
            source: page.lastPhotoPath.length > 0 ? "file://" + page.lastPhotoPath : ""
        }

        Label {
            visible: page.errorText.length > 0
            width: parent.width
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
            font.pixelSize: Theme.fontSizeSmall
            color: Theme.errorColor
            text: page.errorText
        }

        Label {
            visible: !page.recording && page.sessionSummary.length > 0
            width: parent.width
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
            font.pixelSize: Theme.fontSizeExtraSmall
            color: page.sessionCount === 0 ? Theme.errorColor : Theme.secondaryColor
            text: page.sessionSummary
        }

        Label {
            visible: page.recording
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            font.pixelSize: Theme.fontSizeSmall
            color: Theme.secondaryColor
            text: qsTr("%1 photos").arg(page.sessionCount)
        }

        Button {
            anchors.horizontalCenter: parent.horizontalCenter
            text: page.recording ? qsTr("Stop recording") : qsTr("Start recording")
            enabled: page.cameraAvailable && (page.recording || (page.resolutionApplied && !page.captureInProgress && !page.pendingStop))
            onClicked: page.recording ? page.stopRecording() : page.startRecording()
        }
    }

    Camera {
        id: cameraObject

        cameraState: Camera.UnloadedState
        captureMode: Camera.CaptureStillImage
        flash.mode: Camera.FlashOff
        focus.focusMode: Camera.FocusAuto

        onError: page.errorText = errorString

        imageCapture {
            onImageSaved: page.onImageSaved(requestId, path)
            onCaptureFailed: page.onCaptureFailed(requestId, message)
            onReadyForCaptureChanged: page.updateStatus()
        }

        metaData {
            orientation: page.captureOrientation
        }
    }

    CameraCapabilities {
        id: capabilities
        camera: cameraObject
    }

    OrientationSensor {
        id: orientationSensor
        active: false
    }

    Compass {
        id: compass
        active: false
    }

    PositionSource {
        id: positionSource
        active: false
        updateInterval: 1000
        preferredPositioningMethods: PositionSource.AllPositioningMethods
    }

    DisplayBlanking {
        preventBlanking: page.recording
    }

    Timer {
        id: captureTimer
        repeat: false
        onTriggered: page.tryCapture()
    }

    Timer {
        id: focusWatchdog
        interval: 2000
        repeat: false
        onTriggered: {
            if (!page.focusAttemptActive)
                return;
            // Autofocus did not finish in time: skip this picture.
            page.cancelFocus();
            page.updateStatus();
            page.scheduleNext();
        }
    }

    Timer {
        id: clockTimer
        interval: 1000
        repeat: true
        running: page.status === PageStatus.Active
        onTriggered: {
            page.clockTick = Date.now();
            page.updateStatus();
        }
    }

    Timer {
        id: stopTimeout
        interval: 15000
        repeat: false
        onTriggered: {
            if (page.pendingStop)
                page.finalizeSession();
        }
    }
}

const serverUrl = "ws://localhost:8443";  // adjust if needed
const statusDiv = document.getElementById("status");
const videoElement = document.getElementById("remoteVideo");
let ws;
let pc;


function logStatus(message) {
    console.log(message);
    statusDiv.textContent = message;
}

function connect() {
    ws = new WebSocket(serverUrl);

    ws.onopen = () => {
        logStatus("Connected to signaling server.");
        ws.send(`HELLO browser1`);
    };

    ws.onmessage = async (event) => {
    logStatus("Received message from server.");
    console.log("Server says:", event.data);

    const msg = JSON.parse(event.data);

    console.log("Parsed message:", msg);

    if (msg.sdp) {
        if (msg.type === "offer") {
            logStatus("Received SDP offer, creating PeerConnection...");

            // Create the PeerConnection
            pc = new RTCPeerConnection({
                iceServers: [
                    { urls: "stun:stun.l.google.com:19302" }
                ]
            });
            
            // Handle ICE connection state changes
            pc.oniceconnectionstatechange = () => {
                console.log("ICE Connection State:", pc.iceConnectionState);
                if (pc.iceConnectionState === "connected" || pc.iceConnectionState === "completed") {
                    console.log("P2P Connection established! 🎉");
                }
            };

            // Handle local ICE candidates
            pc.onicecandidate = (event) => {
                if (event.candidate) {
                    console.log("Sending ICE candidate to signaling server:", event.candidate);
                    ws.send(JSON.stringify({
                        ice: {
                            sdpMLineIndex: event.candidate.sdpMLineIndex,
                            candidate: event.candidate.candidate
                        }
                    }));
                }
            };

            pc.onconnectionstatechange = () => {
                console.log("Connection state:", pc.connectionState);
            };

            // Create a MediaStream
            pc.ontrack = (event) => {
                console.log("Received remote media:", event);
                videoElement.srcObject = event.streams[0];
            };

            // Set the remote description
            await pc.setRemoteDescription(new RTCSessionDescription({
                type: "offer",
                sdp: msg.sdp
            }));
            console.log("Set remote description.");

            // Create and send the answer
            const answer = await pc.createAnswer();
            await pc.setLocalDescription(answer);
            console.log("Created and set local SDP answer.");

            // Send the SDP answer to the signaling server
            ws.send(JSON.stringify({
                sdp: {
                    type: "answer",
                    sdp: pc.localDescription.sdp
                }
            }));
            console.log("Sent SDP answer.");
        }
    } else if (msg.ice) {
        // Handle incoming ICE candidates
        console.log("Received ICE candidate:", msg.ice);
        try {
            await pc.addIceCandidate(new RTCIceCandidate(msg.ice));
            console.log("Added ICE candidate.");
        } catch (e) {
            console.error("Error adding ICE candidate:", e);
        }
    }


};


    ws.onerror = (error) => {
        logStatus("WebSocket error: " + error.message);
    };

    ws.onclose = () => {
        logStatus("WebSocket closed.");
    };
}

connect();


videoElement.onplaying = () => {
    console.log("🎥 Video is playing!");
};

videoElement.onpause = () => {
    console.log("⏸️ Video paused.");
};

videoElement.onerror = (e) => {
    console.error("🚨 Video error:", e);
};
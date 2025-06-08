const serverUrl = "ws://localhost:8443";  // adjust if needed
const statusDiv = document.getElementById("status");
let ws;

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

    ws.onmessage = (event) => {
        logStatus("Received message from server.");
        console.log("Server says:", event.data);

        // const msg = JSON.parse(event.data);
        // alert(`GStreamer says: ${msg.data}`);
    };

    ws.onerror = (error) => {
        logStatus("WebSocket error: " + error.message);
    };

    ws.onclose = () => {
        logStatus("WebSocket closed.");
    };
}

connect();

# 📡 WebRTC Signaling Server

This is a simple **WebSocket signaling server** designed to support a **GStreamer WebRTC streaming pipeline**. It is adapted from the original [GStreamer WebRTC signaling example](https://gitlab.freedesktop.org/gstreamer/gst-examples/-/tree/discontinued-for-monorepo/webrtc/signalling?ref_type=heads) with modern adjustments to support recent versions of Python’s **asyncio** and the **websockets** library.

---

## 📂 Original Code

The initial version of this server came from:

```
https://gitlab.freedesktop.org/gstreamer/gst-examples/-/tree/discontinued-for-monorepo/webrtc/signalling?ref_type=heads
```

It was designed as a demonstration for:

* Testing GStreamer WebRTC pipelines
* Exchanging SDP offers/answers
* Relaying ICE candidates between peers

---

## 🔧 Key Changes Made

✅ **Converted the server to modern async/await style**

* Original code called `websockets.serve(...)` inside a **synchronous function**, which fails in newer websockets versions because there’s no running event loop at that time.
* I refactored `run()` to be an **async function** and used **asyncio.run()** to properly start the server.

✅ **Removed manual event loop management**

* Original code used `loop = asyncio.get_event_loop()` and `loop.run_forever()`.
* Modern code uses `asyncio.run()` to manage the event loop automatically, making it cleaner and more reliable.

---

## 🚀 How to Run

```bash
python3 simple_server.py
```

The server will listen on `wss://localhost:8443` (SSL certificates required) or plain `ws://` if `--disable-ssl` is used.
You can configure options like port, address, and cert path via command-line arguments.

---

## 📖 Notes

* This server is **for demonstration purposes** — it’s lightweight and does not handle production-grade features like authentication, scale-out, or load balancing.
* It’s intended as a signaling relay for **GStreamer WebRTC pipelines** and **browser WebRTC clients**.

---

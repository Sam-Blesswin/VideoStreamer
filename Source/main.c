#include <libsoup/soup.h>
#include <glib.h>
#include <json-glib/json-glib.h>
#include <gst/gstelement.h>
#include <gst/gstpromise.h>
#include <gst/webrtc/webrtc.h>

// Global pointer to the WebSocket connection
static SoupWebsocketConnection *ws_conn = NULL;

// Reply (SDP Answer, peer ICE candidate) from the server
static void on_server_message(SoupWebsocketConnection *conn, SoupWebsocketDataType type, GBytes *message, gpointer user_data) {
    if (type != SOUP_WEBSOCKET_DATA_TEXT)
        return;

    gsize size;
    const gchar *data = g_bytes_get_data(message, &size);
    gchar *text = g_strndup(data, size);

    g_print("Received message from server: %s\n", text);

    JsonParser *parser = json_parser_new();
    if (!json_parser_load_from_data(parser, text, -1, NULL)) {
        g_printerr("Failed to parse JSON: %s\n", text);
        g_free(text);
        g_object_unref(parser);
        return;
    }

    JsonNode *root = json_parser_get_root(parser);
    JsonObject *object = json_node_get_object(root);

    if (json_object_has_member(object, "sdp")) {
        JsonObject *sdp = json_object_get_object_member(object, "sdp");
        const gchar *type_str = json_object_get_string_member(sdp, "type");
        const gchar *sdp_str = json_object_get_string_member(sdp, "sdp");

        GstSDPMessage *sdp_msg;
        gst_sdp_message_new(&sdp_msg);
        if (gst_sdp_message_parse_buffer((const guint8 *)sdp_str, strlen(sdp_str), sdp_msg) != GST_SDP_OK) {
            g_printerr("Failed to parse SDP message\n");
            gst_sdp_message_free(sdp_msg);
            g_free(text);
            g_object_unref(parser);
            return;
        }

        GstWebRTCSessionDescription *answer = gst_webrtc_session_description_new(GST_WEBRTC_SDP_TYPE_ANSWER, sdp_msg);

        GstPromise *promise = gst_promise_new();
        g_signal_emit_by_name(GST_ELEMENT(user_data), "set-remote-description", answer, promise);
        gst_promise_unref(promise);
        gst_webrtc_session_description_free(answer);

        g_print("SDP answer set on pipeline.\n");
    }

    if (json_object_has_member(object, "ice")) {
        JsonObject *ice = json_object_get_object_member(object, "ice");
        guint sdpMLineIndex = json_object_get_int_member(ice, "sdpMLineIndex");
        const gchar *candidate = json_object_get_string_member(ice, "candidate");

        g_signal_emit_by_name(GST_ELEMENT(user_data), "add-ice-candidate", sdpMLineIndex, candidate);
        g_print("ICE candidate added to pipeline.\n");
    }

    g_free(text);
    g_object_unref(parser);
}

// Callback once WebSocket connection is established
void on_server_connected(SoupSession *session, GAsyncResult *res, gpointer user_data)
{
    GError *error = NULL;

    ws_conn = soup_session_websocket_connect_finish(session, res, &error);
    if (!ws_conn) {
        g_print("Failed to connect: %s\n", error ? error->message : "Unknown error");
        if (error) g_error_free(error);
        return;
    }

    GstElement *webrtcbin = GST_ELEMENT(user_data);

    //tell libsoup’s WebSocket connection
    //Whenever a new WebSocket message arrives from the signaling server, call on_server_message()
    //and pass webrtcbin as user_data.
    g_signal_connect(ws_conn, "message", G_CALLBACK(on_server_message), webrtcbin);

    g_print("Connected to signaling server!\n");

    // Register to signaling server
    soup_websocket_connection_send_text(ws_conn, "HELLO gstreamer");
    
    // Create a session with browser1
    soup_websocket_connection_send_text(ws_conn, "SESSION browser1");

    //Test sending a message to client(browser1)
    // soup_websocket_connection_send_text(ws_conn, "{\"type\":\"hi\",\"data\":\"Hello Browser 1!\"}");
}

// Connect to the signaling server asynchronously
void connect_to_signaling_server(const gchar *server_url, GstElement *webrtcbin)
{
    SoupSession *session = soup_session_new();
    SoupMessage *message = soup_message_new(SOUP_METHOD_GET, server_url);

    g_print("Connecting to signaling server at %s\n", server_url);
    
    soup_session_websocket_connect_async(
        session, message, NULL, NULL, 0, NULL,
        (GAsyncReadyCallback) on_server_connected, webrtcbin);
}


static void on_offer_created(GstPromise *promise, gpointer user_data) {

    GstWebRTCSessionDescription *offer = NULL;

    //extract the SDP offer from the promise reply
    const GstStructure *reply = gst_promise_get_reply(promise);

    gst_structure_get(reply, "offer",
                      GST_TYPE_WEBRTC_SESSION_DESCRIPTION, &offer, NULL);
    gst_promise_unref(promise);

    /* Set the local description */
    //After you get the offer from webrtcbin, you must tell webrtcbin to use this offer as the local description
    //WebRTC requires each peer to store their local SDP (offer or answer). It sets up ICE and DTLS parameters for connectivity
    GstPromise *local_desc_promise = gst_promise_new();
    g_signal_emit_by_name(GST_ELEMENT(user_data), "set-local-description", offer, local_desc_promise);
    gst_promise_unref(local_desc_promise);

    /* Convert SDP to text */
    gchar *sdp_str = gst_sdp_message_as_text(offer->sdp);
    g_print("Sending SDP offer:\n%s\n", sdp_str);

    /* Wrap the SDP offer in JSON */
    JsonObject *sdp = json_object_new();
    json_object_set_string_member(sdp, "type", "offer");
    json_object_set_string_member(sdp, "sdp", sdp_str);

    JsonNode *root = json_node_new(JSON_NODE_OBJECT);
    json_node_set_object(root, sdp);
    JsonGenerator *gen = json_generator_new();
    json_generator_set_root(gen, root);
    gchar *text = json_generator_to_data(gen, NULL);

    /* Send over WebSocket */
    soup_websocket_connection_send_text(ws_conn, text);

    /* Cleanup */
    g_free(sdp_str);
    g_free(text);
    json_object_unref(sdp);
    json_node_free(root);
    g_object_unref(gen);
    gst_webrtc_session_description_free(offer);
}

//sdpMLineIndex is an integer that identifies which media line in the SDP (m=0 => audio or m=1 =>video)
//GStreamer might emit several ICE candidates as it discovers different STUN results or TURN
static void send_ice_candidate_message(GstElement *webrtcbin, guint mlineindex,
                                       gchar *candidate, gpointer user_data) {
    g_print("Sending ICE candidate...\n");
    g_print("mlineindex: %u, candidate: %s\n", mlineindex, candidate);

    /* Wrap the ICE candidate in JSON */

    JsonObject *ice = json_object_new();
    json_object_set_int_member(ice, "sdpMLineIndex", mlineindex);
    json_object_set_string_member(ice, "candidate", candidate);

    JsonNode *root = json_node_new(JSON_NODE_OBJECT);
    json_node_set_object(root, ice);
    JsonGenerator *gen = json_generator_new();
    json_generator_set_root(gen, root);
    gchar *text = json_generator_to_data(gen, NULL);

    /* Send over WebSocket */
    soup_websocket_connection_send_text(ws_conn, text);

    /* Cleanup */
    g_free(text);
    json_object_unref(ice);
    json_node_free(root);
    g_object_unref(gen);
}

static void on_ice_connection_state(GObject *obj, GParamSpec *pspec, gpointer user_data)
{
    GstWebRTCICEConnectionState state;
    g_object_get(obj, "ice-connection-state", &state, NULL);

    if (state == GST_WEBRTC_ICE_CONNECTION_STATE_CONNECTED ||
        state == GST_WEBRTC_ICE_CONNECTION_STATE_COMPLETED) {
        g_print("P2P Connection established!\n");
    }
}


static void on_negotiation_needed(GstElement *webrtcbin, gpointer user_data) {
    g_print("Negotiation needed, creating offer...\n");

    GstPromise *promise = gst_promise_new_with_change_func(on_offer_created, webrtcbin, NULL);

    //webrtcbin automatically creates the offer
    //when the offer is ready, it will emit the "create-offer" signal, then call the on_offer_created callback
    g_signal_emit_by_name(webrtcbin, "create-offer", NULL, promise);
}

int main(int argc, char *argv[])
{
    if (argc < 2) {
        g_print("Usage: %s wss://localhost:8443\n", argv[0]);
        return 1;
    }

    // Initialize GStreamer
    gst_init(&argc, &argv);
    g_print("GStreamer initialized.\n");

    /*Creating Elements*/

    // Source element: captures video from the camera
    GstElement *source = gst_element_factory_make("avfvideosrc", "source");

    //hardware encoder
    // GstElement *encoder = gst_element_factory_make("vtenc_h264", "encoder");

    //software encoder
    GstElement *encoder = gst_element_factory_make("x264enc", "encoder");

    //payloader: converts compressed video (e.g. H.264) into RTP packets suitable for real-time streaming
    GstElement *payloader = gst_element_factory_make("rtph264pay", "payloader");

    //WebRTC bin: handles WebRTC Signaling, ICE and media streaming
    GstElement *webrtcbin = gst_element_factory_make("webrtcbin", "webrtcbin");
    g_object_set(webrtcbin, "stun-server", "stun://stun.l.google.com:19302", NULL);


    if (!source || !encoder || !payloader || !webrtcbin) {
        g_error("Failed to create one or more elements");
        return 1;
    }
    
    //Adding elements to the pipeline
    GstElement *pipeline = gst_pipeline_new("webrtc-pipeline");
    gst_bin_add_many(GST_BIN(pipeline), source, encoder, payloader, webrtcbin, NULL);

    // Linking elements in the pipeline
    if (!gst_element_link(source, encoder))
        g_error("Failed to link source to encoder");

    if (!gst_element_link(encoder, payloader))
        g_error("Failed to link encoder to payloader");

    // Link payloader to webrtcbin sink_%u pad
    GstPad *payloader_srcpad = gst_element_get_static_pad(payloader, "src");

    GstPad *webrtcbin_sinkpad = gst_element_get_request_pad(webrtcbin, "sink_%u");
    if (gst_pad_link(payloader_srcpad, webrtcbin_sinkpad) != GST_PAD_LINK_OK)
        g_error("Failed to link payloader to webrtcbin");

    //This callback is triggered by webrtcbin when it needs to create an offer
    //typically when the pipeline first enters PLAYING state 
    g_signal_connect(webrtcbin, "on-negotiation-needed", G_CALLBACK(on_negotiation_needed), NULL);

    //signal emitted by webrtcbin whenever it finds a new local ICE candidate during the ICE gathering process
    g_signal_connect(webrtcbin, "on-ice-candidate", G_CALLBACK(send_ice_candidate_message), NULL); 

    //properties change in webrtcbin when the ICE connection state changes
    g_signal_connect(webrtcbin, "notify::ice-connection-state", G_CALLBACK(on_ice_connection_state), NULL);


    // Connect to signaling server
    connect_to_signaling_server(argv[1], webrtcbin);

    //Start the pipeline
    gst_element_set_state(pipeline, GST_STATE_PLAYING);
    g_print("Pipeline is playing.\n");

    // Start the GLib main loop
    GMainLoop *loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(loop);

    // Cleanup
    gst_object_unref(payloader_srcpad);
    gst_object_unref(webrtcbin_sinkpad);
    g_main_loop_unref(loop);
    g_object_unref(ws_conn);

    return 0;
}



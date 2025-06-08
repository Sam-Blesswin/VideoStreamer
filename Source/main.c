//static gboolean
//bus_call (GstBus     *bus,
//          GstMessage *msg,
//          gpointer    data)
//{
//  GMainLoop *loop = (GMainLoop *) data;
//
//  switch (GST_MESSAGE_TYPE (msg)) {
//
//    case GST_MESSAGE_EOS:
//      g_print ("End of stream\n");
//      g_main_loop_quit (loop);
//      break;
//
//    case GST_MESSAGE_ERROR: {
//      gchar  *debug;
//      GError *error;
//
//      gst_message_parse_error (msg, &error, &debug);
//      g_free (debug);
//
//      g_printerr ("Error: %s\n", error->message);
//      g_error_free (error);
//
//      g_main_loop_quit (loop);
//      break;
//    }
//    default:
//      break;
//  }
//
//  return TRUE;
//}
//
//
//
//int
//main (int   argc,
//      char *argv[])
//{
//  GMainLoop *loop;
//
//  GstBus *bus;
//  guint bus_watch_id;
//
//  /* Initialisation */
//  gst_init (&argc, &argv);
//
//
//  /* we add a message handler */
//  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
//  bus_watch_id = gst_bus_add_watch (bus, bus_call, loop);
//  gst_object_unref (bus);
//
//    /* Pipeline*/
//    GstElement *pipe;
//
//    pipe = gst_parse_launch ("v4l2src ! queue ! vp8enc ! rtpvp8pay ! "
//        "application/x-rtp,media=video,encoding-name=VP8,payload=96 !"
//        " webrtcbin name=sendrecv", NULL);
//
//    GstElement *webrtc;
//
//    webrtc = gst_bin_get_by_name (GST_BIN (pipe), "sendrecv");
//    g_assert (webrtc != NULL);
//
//    /* This is the gstwebrtc entry point where we create the offer.
//     * It will be called when the pipeline goes to PLAYING. */
//    g_signal_connect (webrtc, "on-negotiation-needed",
//        G_CALLBACK (on_negotiation_needed), NULL);
//
//
//    /* We will transmit this ICE candidate to the remote using some
//     * signalling. Incoming ICE candidates from the remote need to be
//     * added by us too. */
//    g_signal_connect (webrtc, "on-ice-candidate",
//        G_CALLBACK (send_ice_candidate_message), NULL);
//
//    /* Incoming streams will be exposed via this signal */
//    g_signal_connect (webrtc, "pad-added",
//        G_CALLBACK (on_incoming_stream), pipe);
//
//    answer = gst_webrtc_session_description_new (
//        GST_WEBRTC_SDP_TYPE_ANSWER, sdp);
//    g_assert (answer);
//
//    /* Set remote description on our pipeline */
//    g_signal_emit_by_name (webrtc, "set-remote-description",
//        answer, NULL);
//
//
//    /* Lifetime is the same as the pipeline itself */
//    gst_object_unref (webrtc);
//
//
//  g_print ("Deleting pipeline\n");
//
//  g_source_remove (bus_watch_id);
//  g_main_loop_unref (loop);
//
//  return 0;
//}


#include <libsoup/soup.h>
#include <glib.h>

// Global pointer to the WebSocket connection
static SoupWebsocketConnection *ws_conn = NULL;

// Callback for incoming WebSocket messages
void on_server_message(SoupWebsocketConnection *conn,
                      SoupWebsocketDataType type,
                      GBytes *message,
                      gpointer user_data)
{
   if (type != SOUP_WEBSOCKET_DATA_TEXT) {
       g_printerr("Received non-text message, ignoring\n");
       return;
   }

   gsize size;
   const gchar *data = g_bytes_get_data(message, &size);

}

// Callback once WebSocket connection is established
void on_server_connected(SoupSession *session,
                         GAsyncResult *res,
                         SoupMessage *msg)
{
    GError *error = NULL;

    ws_conn = soup_session_websocket_connect_finish(session, res, &error);
    if (!ws_conn) {
        g_print("Failed to connect: %s\n", error ? error->message : "Unknown error");
        if (error) g_error_free(error);
        return;
    }

    g_print("Connected to signaling server!\n");

    // Connect the "message" signal to handle incoming messages
   g_signal_connect(ws_conn, "message", G_CALLBACK(on_server_message), NULL);
}

// Connect to the signaling server asynchronously
void connect_to_signaling_server(const gchar *server_url)
{
    SoupSession *session = soup_session_new();
    SoupMessage *message = soup_message_new(SOUP_METHOD_GET, server_url);

    g_print("Connecting to signaling server at %s\n", server_url);
    
    soup_session_websocket_connect_async(
        session, message, NULL, NULL, 0, NULL,
        (GAsyncReadyCallback) on_server_connected, message);
}

int main(int argc, char *argv[])
{
    if (argc < 2) {
        g_print("Usage: %s wss://localhost:8443\n", argv[0]);
        return 1;
    }

    // Initialize GLib
    g_print("Connecting to signaling server: %s\n", argv[1]);

    // Connect to signaling server
    connect_to_signaling_server(argv[1]);

    // Start the GLib main loop
    GMainLoop *loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(loop);

    // Cleanup
    g_main_loop_unref(loop);

    if (ws_conn) {
        g_object_unref(ws_conn);
    }

    return 0;
}



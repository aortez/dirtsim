#include "HttpServer.h"

#include <cpp-httplib/httplib.h>

#include <atomic>
#include <chrono>
#include <spdlog/spdlog.h>
#include <thread>

namespace DirtSim {
namespace Server {

struct HttpServer::Impl {
    std::atomic<bool> running_{ false };
    std::thread thread_;
    std::unique_ptr<httplib::Server> server_;

    void runServer(int port)
    {
        server_ = std::make_unique<httplib::Server>();

        server_->Get("/", [](const httplib::Request&, httplib::Response& res) {
            res.set_redirect("/garden");
        });

        server_->Get("/garden", [](const httplib::Request&, httplib::Response& res) {
            std::string html = R"HTML(<!DOCTYPE html>
<html>
<head>
    <meta charset="utf-8">
    <title>Dirt Sim Garden</title>
    <!-- WebRTC - native browser support, no external libraries needed. -->
    <style>
        body { font-family: monospace; background: #1a1a1a; color: #00ff00; padding: 20px; }
        h1 { color: #00ff00; margin-bottom: 10px; }
        #status { margin: 10px 0; color: #ffff00; }
        .peers { margin-top: 20px; }
        .peer { border: 1px solid #00ff00; padding: 10px; margin: 10px 0; background: #0a0a0a; }
        .peer.disconnected { border-color: #555; opacity: 0.5; }
        .peer h3 { margin: 0 0 5px 0; color: #00ff00; }
        .peer.disconnected h3 { color: #888; }
        .peer-info { font-size: 12px; margin: 3px 0; }
        .peer.disconnected .peer-info { color: #666; }
        .debug-container { margin-top: 20px; }
        .debug-header { display: flex; justify-content: space-between; align-items: center; margin-bottom: 5px; }
        .debug-header h3 { color: #ff6600; margin: 0; }
        .debug-controls button { margin-left: 5px; padding: 3px 8px; background: #333; color: #ff6600; border: 1px solid #ff6600; cursor: pointer; font-family: monospace; font-size: 11px; }
        .debug-controls button:hover { background: #555; }
        .debug-controls button.active { background: #ff6600; color: #000; }
        #debug { padding: 10px; border: 1px solid #ff6600; background: #0a0a0a; font-size: 10px; color: #ff6600; height: 400px; overflow-y: auto; }
        #debug.paused { border-color: #ffaa00; }
    </style>
</head>
<body>
    <h1>Dirt Sim Garden</h1>
    <div id="status">Discovering...</div>
    <div class="peers" id="peers"></div>
    <div class="debug-container">
        <div class="debug-header">
            <h3>Debug Log</h3>
            <div class="debug-controls">
                <button id="pauseBtn" onclick="togglePause()">Pause</button>
                <button onclick="copyDebugLog(event)">Copy</button>
                <button onclick="clearDebugLog()">Clear</button>
            </div>
        </div>
        <div id="debug"></div>
    </div>
    <script>
        // Timestamp formatting with milliseconds.
        function formatTime(date) {
            return date.toLocaleTimeString() + '.' + date.getMilliseconds().toString().padStart(3, '0');
        }

        function formatElapsed(ms) {
            if (ms < 1000) return ms + 'ms';
            if (ms < 60000) return (ms / 1000).toFixed(1) + 's';
            return Math.floor(ms / 60000) + 'm ' + Math.floor((ms % 60000) / 1000) + 's';
        }

        // Debug log state.
        var debugPaused = false;

        // Debug log controls.
        function togglePause() {
            debugPaused = !debugPaused;
            var pauseBtn = document.getElementById('pauseBtn');
            var debug = document.getElementById('debug');
            if (debugPaused) {
                pauseBtn.textContent = 'Resume';
                pauseBtn.classList.add('active');
                debug.classList.add('paused');
            } else {
                pauseBtn.textContent = 'Pause';
                pauseBtn.classList.remove('active');
                debug.classList.remove('paused');
            }
        }

        function copyDebugLog(e) {
            var debug = document.getElementById('debug');
            var text = debug.innerText;
            navigator.clipboard.writeText(text).then(function() {
                var btn = e.target;
                var oldText = btn.textContent;
                btn.textContent = 'Copied!';
                setTimeout(function() { btn.textContent = oldText; }, 1000);
            }).catch(function(err) {
                alert('Failed to copy: ' + err);
            });
        }

        function clearDebugLog() {
            if (confirm('Clear debug log?')) {
                document.getElementById('debug').innerHTML = '';
            }
        }

        // Debug logging.
        function logDebug(msg) {
            if (debugPaused) return;

            var debug = document.getElementById('debug');
            var now = new Date();
            debug.innerHTML = '[' + formatTime(now) + '] ' + msg + '<br>' + debug.innerHTML;
            // Keep debug log from growing too large.
            var lines = debug.innerHTML.split('<br>');
            if (lines.length > 200) {
                debug.innerHTML = lines.slice(0, 200).join('<br>');
            }
        }

        function logResponse(cmdName, id, response) {
            var msg = cmdName + ' [id=' + id + ']: ';
            if (response.success === false || (response.error && !response.success)) {
                msg += 'ERROR: ' + response.error;
            } else if (response.success === true || response.value !== undefined || response.state !== undefined || response.data !== undefined) {
                msg += 'OK';
                var keys = Object.keys(response).filter(function(k) {
                    return k !== 'success' && k !== 'id' && k !== 'data';
                });
                if (keys.length > 0) {
                    msg += ' (' + keys.slice(0, 4).map(function(k) {
                        var v = response[k];
                        if (typeof v === 'string') return k + '="' + v.substring(0, 15) + (v.length > 15 ? '...' : '') + '"';
                        if (typeof v === 'object') return k + '={...}';
                        return k + '=' + v;
                    }).join(', ') + ')';
                }
                if (response.data) {
                    msg += ' +data[' + response.data.length + ' bytes]';
                }
            } else {
                msg += 'UNKNOWN FORMAT: ' + JSON.stringify(response).substring(0, 50);
            }
            logDebug(msg);
        }

        // Global state tracking.
        var globalState = {
            lastUpdate: null,
            serverLastResponse: null,
            uiLastResponse: null
        };

        // Update the status display (timing info is now in individual peer cards).
        function updateStatusDisplay() {
            var status = document.getElementById('status');
            var parts = [];

            if (globalState.lastUpdate) {
                parts.push('Updated: ' + formatTime(globalState.lastUpdate));
            } else {
                parts.push('Discovering...');
            }

            status.textContent = parts.join(' ');
        }

        // Refresh status display every second.
        setInterval(updateStatusDisplay, 1000);

        // Persistent WebSocket connection manager.
        function createPersistentConnection(name, url, onStatusChange) {
            var conn = {
                name: name,
                url: url,
                socket: null,
                pendingRequests: {},
                connected: false,
                reconnectDelay: 1000,
                maxReconnectDelay: 30000,
                reconnectTimer: null,
                lastResponse: null
            };

            function connect() {
                if (conn.socket && conn.socket.readyState === WebSocket.OPEN) {
                    return;
                }

                logDebug(name + ': Connecting to ' + url);
                conn.socket = new WebSocket(url);

                conn.socket.onopen = function() {
                    logDebug(name + ': Connected (persistent)');
                    conn.connected = true;
                    conn.reconnectDelay = 1000;
                    if (onStatusChange) onStatusChange(true);
                    updateStatusDisplay();
                };

                conn.socket.onmessage = function(event) {
                    var processMessage = function(text) {
                        try {
                            var response = JSON.parse(text);
                            conn.lastResponse = new Date();
                            if (name === 'Server') globalState.serverLastResponse = conn.lastResponse;
                            if (name === 'UI') globalState.uiLastResponse = conn.lastResponse;

                            // Match response to pending request by ID.
                            if (response.id && conn.pendingRequests[response.id]) {
                                var req = conn.pendingRequests[response.id];
                                delete conn.pendingRequests[response.id];
                                clearTimeout(req.timeoutId);
                                logResponse(req.cmdName, response.id, response);
                                if (req.onSuccess && response.success !== false) {
                                    req.onSuccess(response);
                                }
                            } else if (response.id) {
                                logDebug(name + ': Stale response [id=' + response.id + '] (no pending request)');
                            }
                        } catch (e) {
                            logDebug(name + ': JSON parse error: ' + e.message + ' (len=' + text.length + ')');
                        }
                    };

                    if (event.data instanceof Blob) {
                        // Binary message - server broadcasts binary RenderMessages to all clients.
                        // Dashboard doesn't decode these (uses ScreenGrab instead). Ignore silently.
                        return;
                    } else {
                        // Text message - parse as JSON response.
                        processMessage(event.data);
                    }
                };

                conn.socket.onerror = function(e) {
                    logDebug(name + ': WebSocket error: ' + (e.message || 'unknown'));
                };

                conn.socket.onclose = function(e) {
                    logDebug(name + ': Disconnected (code=' + e.code + ', reason=' + (e.reason || 'none') + ')');
                    conn.connected = false;
                    conn.socket = null;
                    if (onStatusChange) onStatusChange(false);
                    updateStatusDisplay();

                    // Fail any pending requests.
                    var pendingIds = Object.keys(conn.pendingRequests);
                    for (var i = 0; i < pendingIds.length; i++) {
                        var id = pendingIds[i];
                        var req = conn.pendingRequests[id];
                        clearTimeout(req.timeoutId);
                        logDebug(name + ': Request ' + req.cmdName + ' [id=' + id + '] failed (disconnected)');
                    }
                    conn.pendingRequests = {};

                    // Schedule reconnect with exponential backoff.
                    logDebug(name + ': Reconnecting in ' + (conn.reconnectDelay / 1000) + 's...');
                    conn.reconnectTimer = setTimeout(function() {
                        connect();
                    }, conn.reconnectDelay);
                    conn.reconnectDelay = Math.min(conn.reconnectDelay * 2, conn.maxReconnectDelay);
                };
            }

            conn.send = function(cmdName, params, onSuccess, timeoutMs) {
                if (!conn.socket || conn.socket.readyState !== WebSocket.OPEN) {
                    logDebug(name + ': Cannot send ' + cmdName + ' - not connected');
                    return false;
                }

                var id = Math.floor(Math.random() * 1000000);
                var cmdObj = { command: cmdName, id: id };
                if (params) {
                    Object.assign(cmdObj, params);
                }

                // Set up timeout for this request.
                var timeoutId = setTimeout(function() {
                    if (conn.pendingRequests[id]) {
                        delete conn.pendingRequests[id];
                        logDebug(name + ': ' + cmdName + ' [id=' + id + '] TIMEOUT after ' + (timeoutMs || 10000) + 'ms');
                    }
                }, timeoutMs || 10000);

                conn.pendingRequests[id] = {
                    cmdName: cmdName,
                    onSuccess: onSuccess,
                    timeoutId: timeoutId,
                    sentAt: new Date()
                };

                logDebug(name + ': Sending ' + cmdName + ' [id=' + id + ']');
                conn.socket.send(JSON.stringify(cmdObj));
                return true;
            };

            conn.isConnected = function() {
                return conn.connected && conn.socket && conn.socket.readyState === WebSocket.OPEN;
            };

            conn.connect = connect;

            // Start connecting.
            connect();

            return conn;
        }

        // Create persistent connections to server and UI.
        var serverConn = createPersistentConnection(
            'Server',
            'ws://' + window.location.hostname + ':8080',
            function(connected) {
                updateStatusDisplay();
                if (connected) {
                    // Discover peers immediately when server connects.
                    discoverPeers();
                }
            }
        );

        var uiConn = createPersistentConnection(
            'UI',
            'ws://' + window.location.hostname + ':7070',
            function(connected) { updateStatusDisplay(); }
        );

        // Display peer information.
        function displayPeers(peers) {
            globalState.lastUpdate = new Date();
            var container = document.getElementById('peers');

            // DON'T clear innerHTML - it destroys the video element and srcObject!
            // Instead, update existing elements or create new ones.

            // Always add localhost peers first (not advertised via mDNS).
            var allPeers = [
                { name: 'Local Physics Server', host: 'localhost', port: 8080, role: 'physics' },
                { name: 'Local UI', host: 'localhost', port: 7070, role: 'ui' }
            ];

            // Add discovered remote peers.
            for (var i = 0; i < peers.length; i++) {
                allPeers.push(peers[i]);
            }

            for (var i = 0; i < allPeers.length; i++) {
                var peer = allPeers[i];

                // Reuse existing div if it exists (preserves video element!).
                var divId = 'peer-' + peer.host + '-' + peer.port;
                var div = document.getElementById(divId);
                if (!div) {
                    div = document.createElement('div');
                    div.className = 'peer';
                    div.id = divId;
                    container.appendChild(div);
                }

                var conn = (peer.port === 8080) ? serverConn : uiConn;
                var isConnected = conn.isConnected();
                var connStatus = isConnected ? 'connected' : 'disconnected';

                // Apply disconnected styling if not connected.
                if (!isConnected) {
                    div.classList.add('disconnected');
                } else {
                    div.classList.remove('disconnected');
                }

                // Show disconnect duration if applicable.
                var statusText = connStatus;
                if (!isConnected && conn.lastResponse) {
                    var disconnectTime = Date.now() - conn.lastResponse.getTime();
                    statusText += ' for ' + formatElapsed(disconnectTime);
                }

                var html = '<h3>' + peer.name + ' <small style="color:#888">(' + statusText + ')</small></h3>' +
                    '<div class="peer-info">Host: ' + peer.host + ':' + peer.port + '</div>' +
                    '<div class="peer-info">Role: ' + peer.role + '</div>' +
                    '<div class="peer-info">State: <span id="state-' + peer.host + '-' + peer.port + '">...</span></div>';

                if (peer.role === 'ui') {
                    html += '<div class="peer-info">Stream: <span id="stream-status-' + peer.host + '-' + peer.port + '">waiting...</span></div>';
                    html += '<video id="video-' + peer.host + '-' + peer.port + '" autoplay muted playsinline style="max-width: 320px; border: 1px solid #00ff00; margin-top: 10px; background: #000; cursor: crosshair;"></video>';
                    html += '<div style="margin-top: 5px;">';
                    html += '<button onclick="startWebRtcStream()" style="padding: 5px 10px; background: #333; color: #0f0; border: 1px solid #0f0; cursor: pointer; margin-right: 5px;">Start Stream</button>';
                    html += '<button onclick="sendExitCommand()" style="padding: 5px 10px; background: #330000; color: #f00; border: 1px solid #f00; cursor: pointer;">Exit</button>';
                    html += '</div>';
                }

                // Only update innerHTML if this is a new div (otherwise we destroy video element!).
                if (!div.querySelector('video')) {
                    div.innerHTML = html;
                }
                else {
                    // Update just the status text, not the whole HTML.
                    var stateSpan = document.getElementById('state-' + peer.host + '-' + peer.port);
                    if (stateSpan) stateSpan.textContent = '...';  // Will be updated by queryStatus.
                }

                // Query status using persistent connections.
                queryStatus(peer);
            }

            updateStatusDisplay();
        }

        function queryStatus(peer) {
            var conn = (peer.port === 8080) ? serverConn : uiConn;
            conn.send('StatusGet', null, function(response) {
                var stateSpan = document.getElementById('state-' + peer.host + '-' + peer.port);
                if (stateSpan) {
                    var state = 'Unknown';
                    if (response.state) {
                        state = response.state;
                    } else if (response.value) {
                        if (response.value.scenario_id) {
                            state = 'Running (' + response.value.scenario_id + ')';
                        } else if (response.value.width !== undefined) {
                            state = 'Idle';
                        }
                    }
                    stateSpan.textContent = state;
                }
            });
        }

        function discoverPeers() {
            serverConn.send('PeersGet', null, function(response) {
                if (response.value && response.value.peers) {
                    displayPeers(response.value.peers);
                } else {
                    // No peers discovered, just show local.
                    displayPeers([]);
                }
            });
        }

        // WebRTC video streaming.
        var peerConnection = null;
        var webrtcClientId = 'browser-' + Math.random().toString(36).substr(2, 9);

        function startWebRtcStream() {
            var statusSpan = document.getElementById('stream-status-localhost-7070');
            var video = document.getElementById('video-localhost-7070');

            if (!uiConn.isConnected()) {
                if (statusSpan) statusSpan.textContent = 'UI not connected';
                return;
            }

            if (peerConnection) {
                logDebug('WebRTC: Closing existing connection');
                peerConnection.close();
            }

            if (statusSpan) statusSpan.textContent = 'requesting...';
            logDebug('WebRTC: Requesting stream for client ' + webrtcClientId);

            // Create peer connection (will be used when we receive offer).
            var config = { iceServers: [] };  // No STUN needed for same network.
            peerConnection = new RTCPeerConnection(config);

            // Handle incoming video track.
            peerConnection.ontrack = function(event) {
                logDebug('WebRTC: Received track: ' + event.track.kind);
                if (event.track.kind === 'video' && video) {
                    video.srcObject = event.streams[0];
                    if (statusSpan) statusSpan.textContent = 'streaming';

                    // Debug: Watch for track/stream ending.
                    event.track.onended = function() {
                        logDebug('WebRTC: Track ENDED!');
                    };
                    event.track.onmute = function() {
                        logDebug('WebRTC: Track MUTED!');
                    };
                    event.streams[0].onremovetrack = function() {
                        logDebug('WebRTC: Stream LOST TRACK!');
                    };
                    event.streams[0].oninactive = function() {
                        logDebug('WebRTC: Stream INACTIVE!');
                    };
                }
            };

            // Handle ICE candidates.
            peerConnection.onicecandidate = function(event) {
                if (event.candidate) {
                    logDebug('WebRTC: Sending ICE candidate');
                    uiConn.send('WebRtcCandidate', {
                        clientId: webrtcClientId,
                        candidate: event.candidate.candidate,
                        mid: event.candidate.sdpMid
                    });
                }
            };

            // Connection state changes.
            peerConnection.onconnectionstatechange = function() {
                logDebug('WebRTC: Connection state: ' + peerConnection.connectionState);
                if (statusSpan) {
                    statusSpan.textContent = peerConnection.connectionState;
                }
            };

            // Request the server to send us an offer.
            uiConn.send('StreamStart', {
                clientId: webrtcClientId
            }, function(response) {
                logDebug('WebRTC: Stream initiation acknowledged, waiting for offer...');
            });
        }

        // Listen for WebRTC offers from server (broadcast via WebSocket).
        function handleWebRtcSignaling(message) {
            try {
                var data = JSON.parse(message);
                var statusSpan = document.getElementById('stream-status-localhost-7070');

                // Server sends offer to us.
                if (data.type === 'offer' && data.clientId === webrtcClientId && peerConnection) {
                    logDebug('WebRTC: Received offer from server');
                    var desc = new RTCSessionDescription({ type: 'offer', sdp: data.sdp });
                    peerConnection.setRemoteDescription(desc).then(function() {
                        logDebug('WebRTC: Remote description (offer) set, creating answer');
                        return peerConnection.createAnswer();
                    }).then(function(answer) {
                        logDebug('WebRTC: Created answer');
                        return peerConnection.setLocalDescription(answer);
                    }).then(function() {
                        logDebug('WebRTC: Sending answer to server');
                        uiConn.send('WebRtcAnswer', {
                            clientId: webrtcClientId,
                            sdp: peerConnection.localDescription.sdp
                        });
                    }).catch(function(error) {
                        logDebug('WebRTC: Error handling offer: ' + error.message);
                        if (statusSpan) statusSpan.textContent = 'error: ' + error.message;
                    });
                }
            } catch (e) {
                // Not a signaling message, ignore.
            }
        }

        // Hook into WebSocket message handler for signaling.
        var originalOnMessage = uiConn.socket ? uiConn.socket.onmessage : null;
        function setupSignalingHook() {
            if (uiConn.socket) {
                var oldHandler = uiConn.socket.onmessage;
                uiConn.socket.onmessage = function(event) {
                    if (typeof event.data === 'string') {
                        handleWebRtcSignaling(event.data);
                    }
                    if (oldHandler) oldHandler.call(uiConn.socket, event);
                };
            }
        }
        setTimeout(setupSignalingHook, 2000);

        // Exit command - sends Exit to UI.
        function sendExitCommand() {
            if (!uiConn.isConnected()) {
                logDebug('Cannot send Exit - UI not connected');
                return;
            }
            uiConn.send('Exit', {}, function(response) {
                logDebug('Exit command sent');
            });
        }

        // Mouse event forwarding to LVGL UI.
        function setupMouseForwarding() {
            var video = document.getElementById('video-localhost-7070');
            if (!video) return;

            function sendMouseEvent(eventType, x, y) {
                if (!uiConn.isConnected()) return;

                // Scale coordinates from video display size to LVGL resolution.
                var scaleX = video.videoWidth / video.clientWidth;
                var scaleY = video.videoHeight / video.clientHeight;
                var lvglX = Math.floor(x * scaleX);
                var lvglY = Math.floor(y * scaleY);

                uiConn.send(eventType, { pixelX: lvglX, pixelY: lvglY });
            }

            video.addEventListener('mousedown', function(e) {
                var rect = video.getBoundingClientRect();
                var x = e.clientX - rect.left;
                var y = e.clientY - rect.top;
                sendMouseEvent('MouseDown', x, y);
            });

            video.addEventListener('mousemove', function(e) {
                var rect = video.getBoundingClientRect();
                var x = e.clientX - rect.left;
                var y = e.clientY - rect.top;
                sendMouseEvent('MouseMove', x, y);
            });

            video.addEventListener('mouseup', function(e) {
                var rect = video.getBoundingClientRect();
                var x = e.clientX - rect.left;
                var y = e.clientY - rect.top;
                sendMouseEvent('MouseUp', x, y);
            });

            logDebug('Mouse forwarding enabled for video element');
        }

        // Set up mouse forwarding after a delay (video element needs to exist).
        setTimeout(setupMouseForwarding, 3000);

        // Refresh peer list periodically (initial discovery happens when server connects).
        setInterval(discoverPeers, 2000);
    </script>
</body>
</html>)HTML";
            res.set_content(html, "text/html");
        });

        spdlog::info("HttpServer: Starting on port {}", port);
        spdlog::info("HttpServer: Dashboard available at http://localhost:{}/garden", port);

        running_ = true;
        if (!server_->listen("0.0.0.0", port)) {
            spdlog::error("HttpServer: Failed to start on port {}", port);
            running_ = false;
        }
    }
};

HttpServer::HttpServer(int port) : pImpl_(std::make_unique<Impl>()), port_(port)
{}

HttpServer::~HttpServer()
{
    stop();
}

bool HttpServer::start()
{
    if (pImpl_->running_) {
        return true;
    }

    pImpl_->thread_ = std::thread([this]() { pImpl_->runServer(port_); });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    return pImpl_->running_;
}

void HttpServer::stop()
{
    if (!pImpl_->running_) {
        return;
    }

    if (pImpl_->server_) {
        pImpl_->server_->stop();
    }

    if (pImpl_->thread_.joinable()) {
        pImpl_->thread_.join();
    }

    pImpl_->running_ = false;
    spdlog::info("HttpServer: Stopped");
}

bool HttpServer::isRunning() const
{
    return pImpl_->running_;
}

} // namespace Server
} // namespace DirtSim

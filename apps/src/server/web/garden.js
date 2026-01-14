// Dirt Sim Garden Dashboard
// WebSocket connections, WebRTC streaming, and peer management.

/* exported clearDebugLog, clearFocus, copyDebugLog, focusPeer, removeMouseForwarding, sendExitCommand, setupMouseForwarding, togglePause, toggleWebRtcStream */

//=============================================================================
// Utility Functions
//=============================================================================

// Timestamp formatting with milliseconds.
function formatTime(date) {
    return date.toLocaleTimeString() + '.' + date.getMilliseconds().toString().padStart(3, '0');
}

function formatElapsed(ms) {
    if (ms < 1000) return ms + 'ms';
    if (ms < 60000) return (ms / 1000).toFixed(1) + 's';
    return Math.floor(ms / 60000) + 'm ' + Math.floor((ms % 60000) / 1000) + 's';
}

//=============================================================================
// Debug Log
//=============================================================================

var debugPaused = false;

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

//=============================================================================
// Global State
//=============================================================================

var globalState = {
    lastUpdate: null,
    serverLastResponse: null,
    uiLastResponse: null
};

let currentFocusedPeer = null;

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

//=============================================================================
// Persistent WebSocket Connection Manager
//=============================================================================

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
                        // Skip logging high-frequency events to reduce noise.
                        if (req.cmdName !== 'MouseMove') {
                            logResponse(req.cmdName, response.id, response);
                        }
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

        // Skip logging high-frequency events to reduce noise.
        if (cmdName !== 'MouseMove') {
            logDebug(name + ': Sending ' + cmdName + ' [id=' + id + ']');
        }
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

//=============================================================================
// Connection Instances
//=============================================================================

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

//=============================================================================
// Peer Display
//=============================================================================

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

    for (var j = 0; j < allPeers.length; j++) {
        var peer = allPeers[j];

        // Reuse existing div if it exists (preserves video element!).
        var divId = 'peer-' + peer.host + '-' + peer.port;
        var div = document.getElementById(divId);
        if (!div) {
            div = document.createElement('div');
            div.className = 'peer';
            div.id = divId;
            container.appendChild(div);

            // Add click handler to focus this peer.
            (function(p) {
                div.addEventListener('click', function(e) {
                    // Don't focus if clicking on a button.
                    if (e.target.tagName === 'BUTTON') return;
                    focusPeer(p);
                });
            })(peer);
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

        // If this is the currently focused peer, update focus view styling too.
        if (currentFocusedPeer &&
            currentFocusedPeer.host === peer.host &&
            currentFocusedPeer.port === peer.port) {
            var focusVideo = document.getElementById('focus-video');
            if (focusVideo) {
                if (!isConnected) {
                    focusVideo.style.filter = 'grayscale(100%) brightness(0.5)';
                } else {
                    focusVideo.style.filter = '';
                }
            }
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
            '<div class="peer-info">State: <span id="state-' + peer.host + '-' + peer.port + '">...</span></div>' +
            '<div class="peer-health">' +
            '<div class="health-bar"><span class="health-label">CPU</span><div class="health-track"><div class="health-fill cpu-fill" id="cpu-' + peer.host + '-' + peer.port + '"></div></div><span class="health-value" id="cpu-val-' + peer.host + '-' + peer.port + '">--</span></div>' +
            '<div class="health-bar"><span class="health-label">MEM</span><div class="health-track"><div class="health-fill mem-fill" id="mem-' + peer.host + '-' + peer.port + '"></div></div><span class="health-value" id="mem-val-' + peer.host + '-' + peer.port + '">--</span></div>' +
            '</div>';

        if (peer.role === 'ui') {
            html += '<div class="peer-info">Stream: <span id="stream-status-' + peer.host + '-' + peer.port + '">waiting...</span></div>';
            html += '<video id="video-' + peer.host + '-' + peer.port + '" autoplay muted playsinline></video>';
            html += '<div class="peer-controls">';
            html += '<button id="stream-btn-' + peer.host + '-' + peer.port + '" onclick="toggleWebRtcStream()">Start Stream</button>';
            html += '<button class="danger" onclick="sendExitCommand()">Exit</button>';
            html += '</div>';
        }

        // Only update innerHTML if this is a new div (otherwise we destroy video element!).
        if (!div.querySelector('video')) {
            div.innerHTML = html;
        } else {
            // Update the header status (connected/disconnected) without destroying video.
            var h3 = div.querySelector('h3');
            if (h3) {
                h3.innerHTML = peer.name + ' <small style="color:#888">(' + statusText + ')</small>';
            }
            // Update the state span.
            var stateSpan = document.getElementById('state-' + peer.host + '-' + peer.port);
            if (stateSpan) stateSpan.textContent = '...'; // Will be updated by queryStatus.
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

        // Update health metrics.
        var cpuPercent = response.cpu_percent || 0;
        var memPercent = response.memory_percent || 0;

        var cpuFill = document.getElementById('cpu-' + peer.host + '-' + peer.port);
        var cpuVal = document.getElementById('cpu-val-' + peer.host + '-' + peer.port);
        var memFill = document.getElementById('mem-' + peer.host + '-' + peer.port);
        var memVal = document.getElementById('mem-val-' + peer.host + '-' + peer.port);

        if (cpuFill) {
            cpuFill.style.width = cpuPercent.toFixed(0) + '%';
            cpuFill.className = 'health-fill cpu-fill' + getHealthClass(cpuPercent);
        }
        if (cpuVal) {
            cpuVal.textContent = cpuPercent.toFixed(0) + '%';
        }
        if (memFill) {
            memFill.style.width = memPercent.toFixed(0) + '%';
            memFill.className = 'health-fill mem-fill' + getHealthClass(memPercent);
        }
        if (memVal) {
            memVal.textContent = memPercent.toFixed(0) + '%';
        }
    });
}

function getHealthClass(percent) {
    if (percent >= 90) return ' critical';
    if (percent >= 75) return ' warning';
    return '';
}

function discoverPeers() {
    var sent = serverConn.send('PeersGet', null, function(response) {
        if (response.value && response.value.peers) {
            displayPeers(response.value.peers);
        } else {
            // No peers discovered, just show local.
            displayPeers([]);
        }
    });

    // If send failed (server disconnected), still update display to show disconnected state.
    if (!sent) {
        displayPeers([]);
    }
}

//=============================================================================
// WebRTC Video Streaming
//=============================================================================

var peerConnection = null;
var streamActive = false;
var webrtcClientId = 'browser-' + Math.random().toString(36).substr(2, 9);

function updateStreamButton(text, enabled) {
    var btn = document.getElementById('stream-btn-localhost-7070');
    if (btn) {
        btn.textContent = text;
        btn.disabled = !enabled;
    }
}

function updateVideoState(state) {
    var video = document.getElementById('video-localhost-7070');
    if (!video) return;

    // Remove all state classes.
    video.classList.remove('streaming', 'stopped', 'error');

    // Add appropriate class.
    if (state) {
        video.classList.add(state);
    }
}

function toggleWebRtcStream() {
    if (streamActive && peerConnection) {
        stopWebRtcStream();
    } else {
        startWebRtcStream();
    }
}

function stopWebRtcStream() {
    var statusSpan = document.getElementById('stream-status-localhost-7070');

    if (peerConnection) {
        logDebug('WebRTC: Stopping stream');
        peerConnection.close();
        peerConnection = null;
        streamActive = false;
        updateStreamButton('Start Stream', true);
        updateVideoState('stopped');
        if (statusSpan) statusSpan.textContent = 'stopped';
    }
}

function startWebRtcStream() {
    var statusSpan = document.getElementById('stream-status-localhost-7070');
    var video = document.getElementById('video-localhost-7070');

    if (!uiConn.isConnected()) {
        if (statusSpan) statusSpan.textContent = 'UI not connected';
        updateStreamButton('Start Stream', false);
        return;
    }

    if (statusSpan) statusSpan.textContent = 'connecting...';
    updateStreamButton('Connecting...', false);
    logDebug('WebRTC: Requesting stream for client ' + webrtcClientId);

    // Create peer connection (will be used when we receive offer).
    var config = { iceServers: [] }; // No STUN needed for same network.
    peerConnection = new RTCPeerConnection(config);

    // Handle incoming video track.
    peerConnection.ontrack = function(event) {
        logDebug('WebRTC: Received track: ' + event.track.kind);
        if (event.track.kind === 'video' && video) {
            video.srcObject = event.streams[0];
            streamActive = true;
            updateStreamButton('Stop Stream', true);
            updateVideoState('streaming');
            if (statusSpan) statusSpan.textContent = 'streaming';

            // If this peer is currently focused, update the focus video too.
            if (currentFocusedPeer && currentFocusedPeer.port === 7070) {
                var focusVideo = document.getElementById('focus-video');
                if (focusVideo) {
                    focusVideo.srcObject = event.streams[0];
                    focusVideo.play().catch(function(err) {
                        logDebug('Focus video play error: ' + err.message);
                    });
                    logDebug('Updated focus video with new stream');
                }
            }

            // Clean up on track end.
            event.track.onended = function() {
                logDebug('WebRTC: Track ended');
                stopWebRtcStream();
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
        var state = peerConnection.connectionState;
        logDebug('WebRTC: Connection state: ' + state);
        if (statusSpan) {
            statusSpan.textContent = state;
        }

        // Update button and video state based on connection.
        if (state === 'connected') {
            streamActive = true;
            updateStreamButton('Stop Stream', true);
            updateVideoState('streaming');
        } else if (state === 'disconnected' || state === 'failed' || state === 'closed') {
            streamActive = false;
            updateStreamButton('Start Stream', true);
            updateVideoState(state === 'failed' ? 'error' : 'stopped');
            if (statusSpan) statusSpan.textContent = state === 'failed' ? 'failed' : 'disconnected';
        }
    };

    // Request stream and process offer synchronously.
    uiConn.send('StreamStart', {
        clientId: webrtcClientId
    }, function(response) {
        if (!response || !response.sdpOffer) {
            logDebug('WebRTC: No SDP offer in StreamStart response');
            updateStreamButton('Start Stream', true);
            updateVideoState('error');
            if (statusSpan) statusSpan.textContent = 'failed';
            return;
        }

        logDebug('WebRTC: Received offer in response (' + response.sdpOffer.length + ' bytes)');
        var desc = new RTCSessionDescription({ type: 'offer', sdp: response.sdpOffer });
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
            logDebug('WebRTC: Error processing offer: ' + error.message);
            updateStreamButton('Start Stream', true);
            updateVideoState('error');
            if (statusSpan) statusSpan.textContent = 'error: ' + error.message;
        });
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
    } catch (_unused) {
        // Not a signaling message, ignore.
    }
}

// Hook into WebSocket message handler for signaling.
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

//=============================================================================
// UI Commands
//=============================================================================

function sendExitCommand() {
    if (!uiConn.isConnected()) {
        logDebug('Cannot send Exit - UI not connected');
        return;
    }
    uiConn.send('Exit', {}, function(response) {
        logDebug('Exit command sent');
    });
}

//=============================================================================
// Mouse Event Forwarding
//=============================================================================

// Active mouse forwarding listeners (so we can clean them up).
var activeMouseListeners = null;

function setupMouseForwarding(videoElement) {
    if (!videoElement) return;

    // Clean up previous listeners if they exist.
    if (activeMouseListeners) {
        removeMouseForwarding();
    }

    // Calculate the actual rendered video position within the element.
    // This handles object-fit: contain letterboxing correctly.
    function getVideoRenderRect(video) {
        var videoWidth = video.videoWidth;
        var videoHeight = video.videoHeight;
        var elementWidth = video.clientWidth;
        var elementHeight = video.clientHeight;

        if (!videoWidth || !videoHeight) {
            // Video not loaded yet, fall back to element size.
            return { x: 0, y: 0, width: elementWidth, height: elementHeight };
        }

        var videoAspect = videoWidth / videoHeight;
        var elementAspect = elementWidth / elementHeight;

        var renderWidth, renderHeight, offsetX, offsetY;

        if (videoAspect > elementAspect) {
            // Video is wider than element - letterbox top/bottom.
            renderWidth = elementWidth;
            renderHeight = elementWidth / videoAspect;
            offsetX = 0;
            offsetY = (elementHeight - renderHeight) / 2;
        } else {
            // Video is taller than element - letterbox left/right.
            renderHeight = elementHeight;
            renderWidth = elementHeight * videoAspect;
            offsetX = (elementWidth - renderWidth) / 2;
            offsetY = 0;
        }

        return { x: offsetX, y: offsetY, width: renderWidth, height: renderHeight };
    }

    var buttonNames = ['LEFT', 'MIDDLE', 'RIGHT'];
    var lastSentPixel = { x: -1, y: -1 };

    function sendMouseEvent(eventType, elementX, elementY, button) {
        if (!uiConn.isConnected()) return;

        var renderRect = getVideoRenderRect(videoElement);

        // Convert element coords to video content coords (accounting for letterbox offset).
        var videoX = elementX - renderRect.x;
        var videoY = elementY - renderRect.y;

        // Check if click is within the actual video content area.
        if (videoX < 0 || videoX > renderRect.width ||
            videoY < 0 || videoY > renderRect.height) {
            // Click is in letterbox area, ignore.
            return;
        }

        // Scale from rendered size to actual video resolution.
        var scaleX = videoElement.videoWidth / renderRect.width;
        var scaleY = videoElement.videoHeight / renderRect.height;
        var lvglX = Math.floor(videoX * scaleX);
        var lvglY = Math.floor(videoY * scaleY);

        // Clamp to valid range.
        lvglX = Math.max(0, Math.min(lvglX, videoElement.videoWidth - 1));
        lvglY = Math.max(0, Math.min(lvglY, videoElement.videoHeight - 1));

        // Deduplicate drag events.
        if (eventType === 'MouseMove' && lvglX === lastSentPixel.x && lvglY === lastSentPixel.y) {
            return;
        }
        lastSentPixel = { x: lvglX, y: lvglY };

        // Debug logging for coordinate mapping.
        if (eventType === 'MouseDown') {
            logDebug('Mouse: element(' + Math.round(elementX) + ',' + Math.round(elementY) + ') ' +
                     'video(' + videoElement.videoWidth + 'x' + videoElement.videoHeight + ') ' +
                     'client(' + videoElement.clientWidth + 'x' + videoElement.clientHeight + ') ' +
                     'render(' + Math.round(renderRect.width) + 'x' + Math.round(renderRect.height) + ' @' + Math.round(renderRect.x) + ',' + Math.round(renderRect.y) + ') ' +
                     '-> lvgl(' + lvglX + ',' + lvglY + ') button=' + button);
        }

        var payload = { pixelX: lvglX, pixelY: lvglY };
        if (button !== undefined && button !== null) {
            payload.button = buttonNames[button] || 'LEFT';
        }
        uiConn.send(eventType, payload);
    }

    var mousedownHandler = function(e) {
        e.preventDefault();
        lastSentPixel = { x: -1, y: -1 };
        var rect = videoElement.getBoundingClientRect();
        var x = e.clientX - rect.left;
        var y = e.clientY - rect.top;
        sendMouseEvent('MouseDown', x, y, e.button);
    };

    var mousemoveHandler = function(e) {
        var rect = videoElement.getBoundingClientRect();
        var x = e.clientX - rect.left;
        var y = e.clientY - rect.top;
        sendMouseEvent('MouseMove', x, y);
    };

    var mouseupHandler = function(e) {
        var rect = videoElement.getBoundingClientRect();
        var x = e.clientX - rect.left;
        var y = e.clientY - rect.top;
        sendMouseEvent('MouseUp', x, y);
    };

    var contextmenuHandler = function(e) {
        e.preventDefault();
    };

    videoElement.addEventListener('mousedown', mousedownHandler);
    videoElement.addEventListener('mousemove', mousemoveHandler);
    videoElement.addEventListener('mouseup', mouseupHandler);
    videoElement.addEventListener('contextmenu', contextmenuHandler);

    // Store references so we can remove them later.
    activeMouseListeners = {
        video: videoElement,
        mousedown: mousedownHandler,
        mousemove: mousemoveHandler,
        mouseup: mouseupHandler,
        contextmenu: contextmenuHandler
    };

    logDebug('Mouse forwarding enabled for focus video');
}

function removeMouseForwarding() {
    if (!activeMouseListeners) return;

    activeMouseListeners.video.removeEventListener('mousedown', activeMouseListeners.mousedown);
    activeMouseListeners.video.removeEventListener('mousemove', activeMouseListeners.mousemove);
    activeMouseListeners.video.removeEventListener('mouseup', activeMouseListeners.mouseup);
    activeMouseListeners.video.removeEventListener('contextmenu', activeMouseListeners.contextmenu);

    activeMouseListeners = null;
    logDebug('Mouse forwarding disabled');
}

//=============================================================================
// Focus View and Resizer
//=============================================================================

function focusPeer(peer) {
    // Prevent duplicate focus operations.
    if (currentFocusedPeer && currentFocusedPeer.host === peer.host && currentFocusedPeer.port === peer.port) {
        return;
    }

    currentFocusedPeer = peer;

    // Enable focus mode (shrinks overview previews).
    document.querySelector('.main-container').classList.add('focus-mode');

    // Update UI - hide empty state, show content.
    document.getElementById('focus-empty').style.display = 'none';
    document.getElementById('focus-content').style.display = 'flex';

    // Get the dedicated focus video element.
    var focusVideo = document.getElementById('focus-video');
    var sourceDiv = document.getElementById('peer-' + peer.host + '-' + peer.port);

    // Only UI peers have video streams.
    if (peer.role === 'ui' && focusVideo) {
        // Copy the MediaStream from the source video.
        var sourceVideo = sourceDiv ? sourceDiv.querySelector('video') : null;
        if (sourceVideo && sourceVideo.srcObject) {
            focusVideo.srcObject = sourceVideo.srcObject;
            focusVideo.play().catch(function(err) {
                logDebug('Focus video play error: ' + err.message);
            });
            logDebug('Copied video stream to focus view');
        } else {
            logDebug('Source video has no srcObject - will auto-start stream');
        }

        // Auto-start stream if not already active.
        if (!streamActive) {
            logDebug('Auto-starting stream for focused UI peer');
            startWebRtcStream();
        }

        // Wait a moment for video to be ready, then set up mouse forwarding.
        setTimeout(function() {
            setupMouseForwarding(focusVideo);
        }, 100);
    }

    // Highlight the focused card in overview.
    var allPeers = document.querySelectorAll('.peer');
    for (var i = 0; i < allPeers.length; i++) {
        allPeers[i].classList.remove('focused');
    }
    if (sourceDiv) {
        sourceDiv.classList.add('focused');
    }

    logDebug('Focused on peer: ' + peer.name);
}

function clearFocus() {
    // Remove mouse forwarding from focus video.
    removeMouseForwarding();

    currentFocusedPeer = null;

    // Disable focus mode (restores overview preview sizes).
    document.querySelector('.main-container').classList.remove('focus-mode');

    // Update UI - show empty state, hide content.
    document.getElementById('focus-empty').style.display = 'flex';
    document.getElementById('focus-content').style.display = 'none';

    // Clear the focus video source.
    var focusVideo = document.getElementById('focus-video');
    if (focusVideo) {
        focusVideo.srcObject = null;
    }

    // Remove highlight from all cards.
    var allPeers = document.querySelectorAll('.peer');
    for (var i = 0; i < allPeers.length; i++) {
        allPeers[i].classList.remove('focused');
    }

    logDebug('Focus cleared');
}

// Resizer drag functionality.
var isResizing = false;
var lastDownX = 0;

document.getElementById('resizer').addEventListener('mousedown', function(e) {
    isResizing = true;
    lastDownX = e.clientX;
    document.getElementById('resizer').classList.add('dragging');
    e.preventDefault();
});

document.addEventListener('mousemove', function(e) {
    if (!isResizing) return;

    var overview = document.querySelector('.overview-column');
    var deltaX = e.clientX - lastDownX;
    lastDownX = e.clientX;

    var currentWidth = overview.offsetWidth;
    var newWidth = currentWidth + deltaX;

    // Constrain width between 200px and 800px.
    if (newWidth >= 200 && newWidth <= 800) {
        overview.style.flex = '0 0 ' + newWidth + 'px';
    }
});

document.addEventListener('mouseup', function() {
    if (isResizing) {
        isResizing = false;
        document.getElementById('resizer').classList.remove('dragging');
    }
});

//=============================================================================
// Initialization
//=============================================================================

// Refresh peer list periodically (initial discovery happens when server connects).
setInterval(discoverPeers, 2000);

from flask import Flask, request, jsonify
import secrets

app = Flask(__name__)

#our KEY HAHAHAHAH
ROBOT_API_KEY = "Liquid-Team"

# Store the latest data in memory
latest_data = None

#our headers / security headers
@app.after_request
def add_security_headers(response):
    response.headers["X-Content-Type-Options"] = "nosniff"
    response.headers["X-Frame-Options"] = "DENY"
    response.headers["Referrer-Policy"] = "no-referrer"
    response.headers["Permissions-Policy"] = "geolocation=(), microphone=(), camera=()"
    response.headers["Cache-Control"] = "no-store"

    nonce = secrets.token_urlsafe(16)
    response.headers["Content-Security-Policy"] = (
        "default-src 'self'; "
        "script-src 'self' 'nonce-" + nonce + "'; "
        "style-src 'self' 'unsafe-inline'; "
        "img-src 'self' data:; "
        "connect-src 'self'; "
        "base-uri 'self'; "
        "frame-ancestors 'none'"
    )

    response.headers["Access-Control-Allow-Origin"] = "*"
    response.headers["Access-Control-Allow-Headers"] = "Content-Type, X-API-Key"
    response.headers["Access-Control-Allow-Methods"] = "GET, POST, OPTIONS"

    return response

def require_api_key():
    received = request.headers.get("X-API-Key", "")
    return received == ROBOT_API_KEY

def is_number(v):
    return isinstance(v, (int, float)) and not isinstance(v, bool)

@app.route('/telemetry', methods=['POST'])
def telemetry():
    global latest_data

    if not require_api_key():
        return jsonify({"error": "Unauthorized"}), 401

    data = request.get_json(silent=True)
    ok, msg = data
    if not ok:
        return jsonify({"error": msg}), 400

    latest_data = data
    return jsonify({"status": "success", "message": "Data received"}), 201

@app.route('/telemetry/latest', methods=['GET'])
def latest_telemetry():
    global latest_data
    
    if latest_data:
        return jsonify(latest_data)
    else:
        return jsonify({"message": "No data received yet"}), 404

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=5000, debug=False)


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

def validate_telemetry(data):   #validate incoming telemetry data
    if not isinstance(data, dict):
        return False, "Body must be a JSON object"

    allowed_top = {"temperature", "position"}
    extra_top = set(data.keys()) - allowed_top
    if extra_top:
        return False, "Unexpected fields: " + ", ".join(sorted(extra_top))

    if "temperature" not in data or not is_number(data["temperature"]):
        return False, "temperature must be a number"

    pos = data.get("position")
    if not isinstance(pos, dict):
        return False, "position must be an object"

    allowed_pos = {"x", "y"}
    extra_pos = set(pos.keys()) - allowed_pos
    if extra_pos:
        return False, "Unexpected position fields: " + ", ".join(sorted(extra_pos))

    if "x" not in pos or "y" not in pos:
        return False, "position must contain x and y"

    if not is_number(pos["x"]) or not is_number(pos["y"]):
        return False, "position.x and position.y must be numbers"

    return True, ""

@app.route('/telemetry', methods=['POST'])
def telemetry():
    global latest_data

    if not require_api_key():
        return jsonify({"error": "Unauthorized"}), 401

    data = request.get_json(silent=True)
    ok, msg = validate_telemetry(data)
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


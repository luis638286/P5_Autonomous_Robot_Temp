from flask import Flask, request, jsonify
import secrets

app = Flask(__name__)

# API Key
ROBOT_API_KEY = "Liquid-Team"

# Store the latest data in memory
latest_data = None

# Security headers
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

    # CORS headers
    response.headers["Access-Control-Allow-Origin"] = "*"
    response.headers["Access-Control-Allow-Headers"] = "Content-Type, X-API-Key"
    response.headers["Access-Control-Allow-Methods"] = "GET, POST, OPTIONS"

    return response

def require_api_key():
    """Check if the request has a valid API key"""
    received = request.headers.get("X-API-Key", "")
    return received == ROBOT_API_KEY

def is_number(v):
    """Check if value is a number"""
    return isinstance(v, (int, float)) and not isinstance(v, bool)

def validate_telemetry_data(data):
    """
    Validate incoming telemetry data structure.
    Returns (True, data) if valid, (False, error_message) if invalid.
    """
    if not data:
        return False, "No data provided"
    
    # Check required fields
    required_fields = ['temperature', 'humidity', 'distance_cm', 'pose', 'timestamp_ms']
    for field in required_fields:
        if field not in data:
            return False, f"Missing required field: {field}"
    
    # Validate temperature
    if not is_number(data['temperature']):
        return False, "Invalid temperature value"
    
    # Validate humidity
    if not is_number(data['humidity']):
        return False, "Invalid humidity value"
    
    # Validate distance
    if not is_number(data['distance_cm']):
        return False, "Invalid distance_cm value"
    
    # Validate pose object
    pose = data.get('pose')
    if not isinstance(pose, dict):
        return False, "pose must be an object"
    
    pose_fields = ['x', 'y', 'theta_deg']
    for field in pose_fields:
        if field not in pose:
            return False, f"Missing pose field: {field}"
        if not is_number(pose[field]):
            return False, f"Invalid pose.{field} value"
    
    # Validate timestamp
    if not isinstance(data['timestamp_ms'], (int, float)):
        return False, "Invalid timestamp_ms value"
    
    return True, data

# Handle OPTIONS preflight requests
@app.route('/telemetry', methods=['OPTIONS'])
@app.route('/telemetry/latest', methods=['OPTIONS'])
def handle_options():
    """Handle CORS preflight requests"""
    return '', 204

@app.route('/telemetry', methods=['POST'])
def telemetry():
    """Receive telemetry data from robot"""
    global latest_data

    # Check API key
    if not require_api_key():
        return jsonify({"error": "Unauthorized - Invalid API Key"}), 401

    # Get JSON data
    data = request.get_json(silent=True)
    
    if data is None:
        return jsonify({"error": "Invalid JSON"}), 400

    # Validate data structure
    valid, result = validate_telemetry_data(data)
    
    if not valid:
        return jsonify({"error": result}), 400

    # Store the validated data
    latest_data = data
    
    print(f"✅ Received telemetry: T={data['temperature']}°C, "
          f"Pos=({data['pose']['x']:.3f}, {data['pose']['y']:.3f}), "
          f"θ={data['pose']['theta_deg']:.1f}°")
    
    return jsonify({
        "status": "success", 
        "message": "Data received and validated"
    }), 200

@app.route('/telemetry/latest', methods=['GET'])
def latest_telemetry():
    """Get the latest telemetry data"""
    global latest_data
    
    if latest_data:
        return jsonify(latest_data), 200
    else:
        return jsonify({"message": "No data received yet"}), 404

@app.route('/health', methods=['GET'])
def health():
    """Health check endpoint"""
    return jsonify({
        "status": "healthy",
        "has_data": latest_data is not None
    }), 200

@app.route('/', methods=['GET'])
def index():
    """Root endpoint with API info"""
    return jsonify({
        "name": "Robot Telemetry API",
        "version": "1.0",
        "endpoints": {
            "POST /telemetry": "Submit telemetry data (requires X-API-Key header)",
            "GET /telemetry/latest": "Get latest telemetry data",
            "GET /health": "Health check"
        }
    }), 200

if __name__ == '__main__':
    print("🚀 Robot Telemetry API Starting...")
    print(f"📡 Listening on http://0.0.0.0:5000")
    print(f"🔑 API Key: {ROBOT_API_KEY}")
    app.run(host='0.0.0.0', port=5000, debug=True)
#!/bin/bash

MAX_RETRIES=2
TIMEOUT=30

log() {
  echo -e "[$(date +'%F %T')] [$1] $2"
}

log_debug() {
  log "DEBUG" "$1"
}

log_info() {
  log "INFO" "$1"
}

log_error() {
  log "ERROR" "$1"
}

# Start of script
log_info "Starting healthcheck script"

# Config and ID extraction
config_dir="${CONFIG_DIR:-/etc/rtmpserver}"
config_file="$config_dir/config.json"
id_file="$config_dir/id.txt"

log_debug "Using config directory: $config_dir"
log_debug "Using config file: $config_file"
log_debug "Using ID file: $id_file"

if [[ ! -f "$config_file" ]]; then
  log_error "Config file not found: $config_file"
  exit 1
fi

api_port=$(jq -r '.config.controller.port // "4242"' "$config_file")
http_port=$(jq -r '.config.protocols[] | select(.connector == "HTTP") | .port // "8080"' "$config_file")

log_info "Extracted ports: API=$api_port, HTTP=$http_port"

ENDPOINTS=(
  "POST http://localhost:$api_port/api2|{\"save\":true}"
  "GET http://localhost:$http_port|"
)

service_id="$(hostname)"
if [[ -f "$id_file" ]]; then
  service_id=$(<"$id_file")
  log_info "Loaded service ID from file: $service_id"
else
  log_info "Using hostname as service ID: $service_id"
fi

# Slack webhook payload
webhook=""
payload=""

check_endpoint() {
  local descriptor=$1
  local method="${descriptor%% *}"
  local url_payload="${descriptor#* }"
  local url="${url_payload%%|*}"
  local payload="${url_payload#*|}"
  local retries=0
  local port=$(echo "$url" | awk -F '[:/]' '{print $5}')

  log_info "Checking endpoint $url (method: $method, port: $port)"

  until [ $retries -ge $MAX_RETRIES ]; do
    if [[ "$method" == "POST" ]]; then
      http_status=$(curl --output /dev/null --silent --write-out "%{http_code}" \
        -X POST -H "Content-Type: application/json" \
        --data "$payload" --max-time "$TIMEOUT" "$url")
    else
      http_status=$(curl --output /dev/null --silent --write-out "%{http_code}" \
        --max-time "$TIMEOUT" "$url")
    fi

    log_debug "HTTP status for $url: $http_status"

    if [ "$http_status" -eq 200 ] || [ "$http_status" -eq 415 ]; then
      log_info "$port is healthy (HTTP $http_status)"
      return 0
    else
      log_error "Attempt $((retries + 1)) failed on $port (HTTP $http_status)"
      sleep "$TIMEOUT"
      retries=$((retries + 1))
    fi
  done

  log_error "All attempts failed for $port"
  return 1
}

check_endpoints_parallel() {
  local bg_jobs=()

  log_info "Launching parallel health checks for all endpoints..."
  for endpoint in "${ENDPOINTS[@]}"; do
    check_endpoint "$endpoint" &
    bg_jobs+=($!)
  done

  for job in "${bg_jobs[@]}"; do
    wait "$job" || {
      log_error "At least one endpoint check failed. Initiating recovery sequence."

      log_info "Sending notification to Slack..."
      curl -m 5 -s -X POST --data-urlencode "payload=$payload" "$webhook"
      log_info "Slack notification sent (or attempted)."

      # RAM check
      read total used < <(free | awk '/^Mem:/ {print $2, $3}')
      ram_usage=$(( 100 * used / total ))

      log_debug "RAM total: $total KB, used: $used KB, usage: $ram_usage%"

      if [ "$ram_usage" -gt 99 ]; then
        log_error "RAM usage exceeds 99%: ${ram_usage}%"
        log_info "Killing all Mist* processes and clearing /dev/shm"
        pkill -9 Mist* && log_info "Mist processes killed" || log_error "Failed to kill Mist processes"
        rm -rf /dev/shm/* && log_info "/dev/shm cleared" || log_error "Failed to clear /dev/shm"
      else
        log_info "RAM usage is under control: ${ram_usage}%"
      fi

      log_info "Restarting controller service..."
      bash /restartcontroller.sh && log_info "Restart completed" || log_error "Restart failed"
      exit 1
    }
  done

  log_info "All endpoint checks succeeded. Exiting normally."
  exit 0
}

check_endpoints_parallel

#include "WebUI.h"
#include "ModbusClient.h"
#include "GenericModbusDevice.h"
#include <ESPLogger.h>
#include <ArduinoJson.h>

// Filename validator lives on GenericModbusDevice (single source of truth,
// also called by InverterFactory when loading json:<filename> from config).

void WebUI::handleDiagnostics()
{
    // Check if diagnostics are enabled
    if (!config.getBool("enable_diagnostics", false))
    {
        server.send(403, "text/html",
                    getHTMLHeader("Diagnostics Disabled") +
                        "<div class='container'>"
                        "<h2>⚠️ Diagnostics Disabled</h2>"
                        "<p>Diagnostic tools are disabled. Enable them in config:</p>"
                        "<pre>\"enable_diagnostics\": true</pre>"
                        "<p><a href='/'>← Back to Config</a></p>"
                        "</div>" +
                        getHTMLFooter());
        return;
    }

    String html = getHTMLHeader("Modbus Diagnostics");
    html += R"(
<style>
.diag-section{border:1px solid #ddd;border-radius:8px;padding:1rem;margin:1rem 0;background:#f9f9f9;}
.diag-section h3{margin-top:0;color:#333;}
/* Mobile-first: stacked label + full-width control. Desktop sees the
   inline-block layout via the @media block below. */
.form-group{margin:0.75rem 0;}
.form-group label{display:block;font-weight:bold;margin-bottom:0.25rem;}
.form-group input,.form-group select{width:100%;padding:0.5rem;border:1px solid #ccc;border-radius:4px;font-size:0.875rem;}
.diag-num-narrow{max-width:6rem;display:inline-block;}
.btn-diag{padding:0.625rem 1rem;margin:0.25rem 0.25rem 0.25rem 0;border:none;border-radius:4px;cursor:pointer;font-size:0.875rem;}
.btn-primary{background:#007bff;color:white;}
.btn-secondary{background:#6b7280;color:white;}
.btn-warning{background:#ffc107;color:#000;}
.btn-danger{background:#dc3545;color:white;}
.result-box{margin:0.75rem 0;padding:0.75rem;background:#fff;border:1px solid #ddd;border-radius:4px;font-family:monospace;font-size:12px;max-height:400px;overflow-x:auto;overflow-y:auto;word-break:break-word;}
.success{color:green;}
.error{color:red;}
.frame-log{font-size:11px;}
.frame-tx{color:#0066cc;}
.frame-rx{color:#009900;}
.frame-err{color:#cc0000;}
@media(min-width:640px){
  .form-group label{display:inline-block;width:150px;margin-bottom:0;vertical-align:middle;}
  .form-group input,.form-group select{width:auto;min-width:200px;}
}
@media(max-width:768px){
  .btn-diag{display:block;width:100%;margin:0.25rem 0;}
  /* iOS auto-zoom avoidance — 16px only on mobile, desktop keeps 0.875rem density. */
  .form-group input,.form-group select,.diag-num-narrow{font-size:16px;}
}
</style>

<div class='container'>
<h2>🔧 Modbus Diagnostics</h2>
<p style='background:#fff3cd;padding:10px;border-radius:4px;'>
⚠️ <strong>Warning:</strong> These tools are for troubleshooting only. Baud rate changes require reboot.
</p>

<!-- Manual Register Probe -->
<div class='diag-section'>
<h3>1. Manual Register Probe</h3>
<p>Test a specific Modbus register</p>
<div class='form-group'>
<label>Slave Address:</label>
<input type='number' id='probe_slave' value='3' min='1' max='247'>
</div>
<div class='form-group'>
<label>Register Address:</label>
<input type='text' id='probe_addr' value='0x0000' placeholder='0x0000'>
</div>
<div class='form-group'>
<label>Function Code:</label>
<select id='probe_fc'>
<option value='3'>FC 0x03 (Holding)</option>
<option value='4'>FC 0x04 (Input)</option>
</select>
</div>
<div class='form-group'>
<label>Register Count:</label>
<input type='number' id='probe_count' value='1' min='1' max='10'>
</div>
<button class='btn-diag btn-primary' onclick='probeRegister()'>🔍 Probe Register</button>
<div id='probe_result' class='result-box' style='display:none;'></div>
</div>

<!-- Slave Address Scanner -->
<div class='diag-section'>
<h3>2. Slave Address Scanner</h3>
<p>Find which slave addresses respond (tests register 0x0000)</p>
<div class='form-group'>
<label>Address Range:</label>
<input type='number' id='scan_start' value='1' min='1' max='247' class='diag-num-narrow'>
to
<input type='number' id='scan_end' value='10' min='1' max='247' class='diag-num-narrow'>
</div>
<button class='btn-diag btn-primary' onclick='scanSlaves()'>🔍 Scan Slave Addresses</button>
<div id='slave_result' class='result-box' style='display:none;'></div>
</div>

<!-- Register Range Scanner -->
<div class='diag-section'>
<h3>3. Register Range Scanner</h3>
<p>Scan a range of registers</p>
<div class='form-group'>
<label>Slave Address:</label>
<input type='number' id='range_slave' value='3' min='1' max='247'>
</div>
<div class='form-group'>
<label>Start Address:</label>
<input type='text' id='range_start' value='0x0000' placeholder='0x0000'>
</div>
<div class='form-group'>
<label>End Address:</label>
<input type='text' id='range_end' value='0x0010' placeholder='0x0010'>
</div>
<div class='form-group'>
<label>Function Code:</label>
<select id='range_fc'>
<option value='3'>FC 0x03 (Holding)</option>
<option value='4'>FC 0x04 (Input)</option>
</select>
</div>
<div class='form-group'>
<label>Registers per read:</label>
<input type='number' id='range_count' value='1' min='1' max='10'>
</div>
<button class='btn-diag btn-primary' onclick='scanRange()'>🔍 Scan Register Range</button>
<div id='range_result' class='result-box' style='display:none;'></div>
</div>

<!-- JSON Device Manager -->
<div class='diag-section'>
<h3>4. Custom Device JSON Manager</h3>
<p>Upload JSON device configurations to /devices/ directory</p>

<div style='margin:10px 0;'>
<strong>Upload JSON Device Config:</strong><br>
<input type='file' id='json_file' accept='.json' style='margin:10px 0;'>
<button class='btn-diag btn-primary' onclick='uploadJSON()'>📤 Upload JSON</button>
</div>

<div id='upload_result' class='result-box' style='display:none;'></div>

<div style='margin:20px 0;'>
<strong>Uploaded Devices:</strong><br>
<button class='btn-diag btn-secondary' onclick='listJSONDevices()'>📋 List Files</button>
<button class='btn-diag btn-danger' onclick='deleteJSON()'>🗑️ Delete Selected</button>
</div>

<div id='json_list' class='result-box' style='display:none;'></div>
</div>

<!-- Frame Log Viewer -->
<div class='diag-section'>
<h3>5. Modbus Frame Log (Last 100)</h3>
<button class='btn-diag btn-primary' onclick='refreshFrameLog()'>🔄 Refresh Log</button>
<button class='btn-diag btn-danger' onclick='clearFrameLog()'>🗑️ Clear Log</button>
<div id='frame_log' class='result-box frame-log' style='display:none;'></div>
</div>

</div>

<script>
async function apiCall(action, data) {
    const resp = await fetch('/api/diagnostics', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({action, ...data})
    });
    return await resp.json();
}

async function probeRegister() {
    const slave = parseInt(document.getElementById('probe_slave').value);
    const addr = document.getElementById('probe_addr').value;
    const fc = parseInt(document.getElementById('probe_fc').value);
    const count = parseInt(document.getElementById('probe_count').value);

    document.getElementById('probe_result').innerHTML = '⏳ Probing...';
    document.getElementById('probe_result').style.display = 'block';

    const result = await apiCall('probe', {slave, addr, fc, count});
    document.getElementById('probe_result').innerHTML = result.html;
}

async function scanSlaves() {
    const start = parseInt(document.getElementById('scan_start').value);
    const end = parseInt(document.getElementById('scan_end').value);

    document.getElementById('slave_result').innerHTML = '⏳ Scanning addresses ' + start + '-' + end + '...';
    document.getElementById('slave_result').style.display = 'block';

    const result = await apiCall('scan_slaves', {start, end});
    document.getElementById('slave_result').innerHTML = result.html;
}

async function scanRange() {
    const slave = parseInt(document.getElementById('range_slave').value);
    const start = document.getElementById('range_start').value;
    const end = document.getElementById('range_end').value;
    const fc = parseInt(document.getElementById('range_fc').value);
    const count = parseInt(document.getElementById('range_count').value);

    document.getElementById('range_result').innerHTML = '⏳ Scanning...';
    document.getElementById('range_result').style.display = 'block';

    const result = await apiCall('scan_range', {slave, start, end, fc, count});
    document.getElementById('range_result').innerHTML = result.html;
}

async function refreshFrameLog() {
    document.getElementById('frame_log').innerHTML = '⏳ Loading...';
    document.getElementById('frame_log').style.display = 'block';

    const result = await apiCall('frame_log', {});
    document.getElementById('frame_log').innerHTML = result.html;
}

async function clearFrameLog() {
    if (!confirm('Clear frame log?')) return;
    await apiCall('clear_log', {});
    document.getElementById('frame_log').innerHTML = '<span class="success">✓ Log cleared</span>';
}

async function uploadJSON() {
    const fileInput = document.getElementById('json_file');
    const file = fileInput.files[0];

    if (!file) {
        alert('Please select a JSON file');
        return;
    }

    document.getElementById('upload_result').innerHTML = '⏳ Uploading...';
    document.getElementById('upload_result').style.display = 'block';

    const reader = new FileReader();
    reader.onload = async function(e) {
        const content = e.target.result;
        const result = await apiCall('upload_json', {filename: file.name, content});
        document.getElementById('upload_result').innerHTML = result.html;
        fileInput.value = ''; // Clear file input
    };
    reader.readAsText(file);
}

async function listJSONDevices() {
    document.getElementById('json_list').innerHTML = '⏳ Loading...';
    document.getElementById('json_list').style.display = 'block';

    const result = await apiCall('list_json', {});
    document.getElementById('json_list').innerHTML = result.html;
}

async function deleteJSON() {
    const filename = prompt('Enter filename to delete (e.g., my_device.json):');
    if (!filename) return;

    if (!confirm('Delete ' + filename + '?')) return;

    const result = await apiCall('delete_json', {filename});
    alert(result.html);
    listJSONDevices(); // Refresh list
}
</script>
)";
    html += getHTMLFooter();
    server.send(200, "text/html", html);
}

void WebUI::handleDiagnosticsAPI()
{
    if (!config.getBool("enable_diagnostics", false))
    {
        server.send(403, "application/json", "{\"error\":\"Diagnostics disabled\"}");
        return;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, server.arg("plain"));

    if (error)
    {
        server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        return;
    }

    String action = doc["action"].as<String>();
    String html = "";

    if (action == "probe")
    {
        // Manual register probe
        uint8_t slave = doc["slave"];
        String addrStr = doc["addr"].as<String>();
        uint16_t addr = (uint16_t)strtol(addrStr.c_str(), NULL, 0); // Parse hex or decimal
        uint8_t fc = doc["fc"];
        uint16_t count = doc["count"];

        uint16_t values[10];
        bool success = false;

        // Diagnostics: single attempt, no retries (faster, cleaner logs)
        if (fc == 3)
            success = modbusClient.readHoldingRegisters(slave, addr, count, values, 1);
        else if (fc == 4)
            success = modbusClient.readInputRegisters(slave, addr, count, values, 1);

        if (success)
        {
            html = "<span class='success'>✓ Success!</span><br>";
            for (int i = 0; i < count; i++)
            {
                html += String("Register 0x") + String(addr + i, HEX) + " = 0x" + String(values[i], HEX) + " (" + String(values[i]) + ")<br>";
            }
        }
        else
        {
            html = "<span class='error'>✗ Failed</span><br>Check slave address, register, or function code.";
        }
    }
    else if (action == "scan_slaves")
    {
        uint8_t start = doc["start"];
        uint8_t end = doc["end"];

        html = "<strong>Scanning slave addresses " + String(start) + "-" + String(end) + ":</strong><br><br>";
        int found = 0;

        for (uint8_t addr = start; addr <= end; addr++)
        {
            // Try multiple common registers - some devices only respond to specific ranges
            uint16_t val;
            bool responded = false;
            String registerTested = "";

            // Test registers: 0x0000, 0x0526 (31319), 0x0100 (common status)
            const uint16_t testRegisters[] = {0x0000, 0x0526, 0x0100};

            for (uint16_t testReg : testRegisters)
            {
                size_t frameCountBefore = modbusClient.getFrameLog().size();
                // Diagnostics: single attempt, no retries
                modbusClient.readHoldingRegisters(addr, testReg, 1, &val, 1);
                size_t frameCountAfter = modbusClient.getFrameLog().size();

                if (frameCountAfter > frameCountBefore)
                {
                    auto frames = modbusClient.getFrameLog();
                    auto lastFrame = frames.back();

                    // Check if it was a response (not timeout)
                    if (!lastFrame.isTx && lastFrame.hexDump != "TIMEOUT")
                    {
                        responded = true;
                        registerTested = "0x" + String(testReg, HEX);

                        if (lastFrame.success)
                        {
                            html += "<span class='success'>✓ Slave " + String(addr) + " - Register " + registerTested + " OK</span><br>";
                        }
                        else if (lastFrame.exceptionCode == 2)
                        {
                            html += "<span class='success'>✓ Slave " + String(addr) + " - Responds (Exception 0x02 on " + registerTested + ")</span><br>";
                        }
                        else
                        {
                            html += "<span class='success'>✓ Slave " + String(addr) + " - Responds (Exception 0x" + String(lastFrame.exceptionCode, HEX) + " on " + registerTested + ")</span><br>";
                        }
                        found++;
                        break; // Found a response, no need to test more registers
                    }
                }
                delay(20); // Small delay between register tests
            }

            if (!responded)
            {
                // No response on any test register - likely no device at this address
            }

            delay(50); // Small delay between slave addresses
        }

        if (found == 0)
            html += "<span class='error'>✗ No slaves responded (all timeouts)</span>";
        else
            html += "<br><strong>Found " + String(found) + " responding slave(s)</strong>";
    }
    else if (action == "scan_range")
    {
        uint8_t slave = doc["slave"];
        String startStr = doc["start"].as<String>();
        String endStr = doc["end"].as<String>();
        uint16_t start = (uint16_t)strtol(startStr.c_str(), NULL, 0);
        uint16_t end = (uint16_t)strtol(endStr.c_str(), NULL, 0);
        uint8_t fc = doc["fc"];
        uint16_t count = doc["count"];

        html = "<strong>Scanning 0x" + String(start, HEX) + " to 0x" + String(end, HEX) + ":</strong><br><br>";
        int found = 0;

        for (uint16_t addr = start; addr <= end; addr++)
        {
            uint16_t values[10];
            bool success = false;

            // Diagnostics: single attempt, no retries
            if (fc == 3)
                success = modbusClient.readHoldingRegisters(slave, addr, count, values, 1);
            else if (fc == 4)
                success = modbusClient.readInputRegisters(slave, addr, count, values, 1);

            if (success)
            {
                html += "<span class='success'>✓ 0x" + String(addr, HEX) + " = ";
                for (int i = 0; i < count; i++)
                {
                    if (i > 0) html += ", ";
                    html += "0x" + String(values[i], HEX);
                }
                html += "</span><br>";
                found++;
            }
            delay(10);
        }

        if (found == 0)
            html += "<span class='error'>✗ No registers responded</span>";
        else
            html += "<br><strong>Found " + String(found) + " working register(s)</strong>";
    }
    else if (action == "frame_log")
    {
        std::vector<ModbusFrameLog> log = modbusClient.getFrameLog();

        if (log.empty())
        {
            html = "<span class='error'>No frames logged yet</span>";
        }
        else
        {
            html = "<strong>Last " + String(log.size()) + " frames:</strong><br><br>";
            for (const auto &frame : log)
            {
                String cls = frame.isTx ? "frame-tx" : (frame.success ? "frame-rx" : "frame-err");
                String dir = frame.isTx ? "TX" : "RX";
                String status = frame.success ? "OK" : (frame.exceptionCode > 0 ? "EX" + String(frame.exceptionCode) : "TIMEOUT");

                html += "<span class='" + cls + "'>";
                html += "[" + String(frame.timestamp) + "ms] " + dir + " | ";
                html += "Slave:" + String(frame.slaveId) + " FC:0x" + String(frame.functionCode, HEX) + " ";
                html += "Addr:0x" + String(frame.startAddr, HEX) + " Count:" + String(frame.count) + " | ";
                html += status;
                if (!frame.isTx && frame.responseTimeMs > 0)
                    html += " (" + String(frame.responseTimeMs) + "ms)";
                html += "<br>" + frame.hexDump;
                html += "</span><br><br>";
            }
        }
    }
    else if (action == "clear_log")
    {
        modbusClient.clearFrameLog();
        html = "<span class='success'>✓ Frame log cleared</span>";
    }
    else if (action == "upload_json")
    {
        // Extract what we need from the envelope, then doc.clear() to release
        // the envelope's heap pool before validateJSON parses the device JSON
        // again. Otherwise a 16 KB upload holds three simultaneous live copies
        // (envelope doc, content String, validateJSON's inner doc) and peaks
        // ~50-70 KB heap on a board with ~100 KB free after WiFi/MQTT.
        String filename = doc["filename"].as<String>();
        String content  = doc["content"].as<String>();
        doc.clear();

        if (!GenericModbusDevice::isSafeDeviceFilename(filename))
        {
            html = "<span class='error'>✗ Invalid filename: must be a plain .json filename, "
                   "no slashes, no '..', no leading dot</span>";
        }
        else if (content.length() > GenericModbusDevice::MAX_JSON_FILE_BYTES)
        {
            html = "<span class='error'>✗ File too large: " + String((unsigned)content.length()) +
                   " bytes (max " + String((unsigned)GenericModbusDevice::MAX_JSON_FILE_BYTES) + ")</span>";
        }
        else
        {
            // Validate JSON content (schema, registers, groups, bits, names, sizes)
            // before writing — surface errors in the HTTP response, not just the log.
            String validateErr;
            if (!GenericModbusDevice::validateJSON(content, validateErr))
            {
                html = "<span class='error'>✗ Invalid device JSON: " + validateErr + "</span>";
            }
            else if (!LittleFS.begin())
            {
                html = "<span class='error'>✗ LittleFS mount failed</span>";
            }
            else
            {
                if (!LittleFS.exists("/devices"))
                {
                    LittleFS.mkdir("/devices");
                }

                String filepath = "/devices/" + filename;
                File file = LittleFS.open(filepath, "w");
                if (file)
                {
                    file.print(content);
                    file.close();
                    html = "<span class='success'>✓ Uploaded: " + filename + "</span><br>";
                    html += "File saved to: " + filepath + "<br>";
                    html += "To use: Set device_model = \"json:" + filename + "\" in config";
                }
                else
                {
                    html = "<span class='error'>✗ Failed to write file</span>";
                }
            }
        }
    }
    else if (action == "list_json")
    {
        if (!LittleFS.begin())
        {
            html = "<span class='error'>LittleFS mount failed</span>";
        }
        else
        {
            if (!LittleFS.exists("/devices"))
            {
                html = "<span class='error'>No /devices directory. Upload a file first.</span>";
            }
            else
            {
                File dir = LittleFS.open("/devices");
                if (dir && dir.isDirectory())
                {
                    html = "<strong>JSON device files:</strong><br><br>";
                    int count = 0;

                    File file = dir.openNextFile();
                    while (file)
                    {
                        if (!file.isDirectory() && String(file.name()).endsWith(".json"))
                        {
                            html += "📄 " + String(file.name()) + " (" + String(file.size()) + " bytes)<br>";
                            count++;
                        }
                        file = dir.openNextFile();
                    }

                    if (count == 0)
                    {
                        html += "<span class='error'>No .json files found</span>";
                    }
                    else
                    {
                        html += "<br><strong>Total: " + String(count) + " file(s)</strong>";
                    }
                }
                else
                {
                    html = "<span class='error'>Failed to open /devices directory</span>";
                }
            }
        }
    }
    else if (action == "delete_json")
    {
        String filename = doc["filename"].as<String>();

        if (!GenericModbusDevice::isSafeDeviceFilename(filename))
        {
            html = "<span class='error'>✗ Invalid filename</span>";
        }
        else if (!LittleFS.begin())
        {
            html = "<span class='error'>LittleFS mount failed</span>";
        }
        else
        {
            String filepath = "/devices/" + filename;
            if (LittleFS.exists(filepath))
            {
                if (LittleFS.remove(filepath))
                {
                    html = "<span class='success'>✓ Deleted: " + filename + "</span>";
                }
                else
                {
                    html = "<span class='error'>✗ Failed to delete file</span>";
                }
            }
            else
            {
                html = "<span class='error'>✗ File not found: " + filename + "</span>";
            }
        }
    }
    else
    {
        html = "<span class='error'>Unknown action</span>";
    }

    JsonDocument response;
    response["html"] = html;
    String output;
    serializeJson(response, output);
    server.send(200, "application/json", output);
}

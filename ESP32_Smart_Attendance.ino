#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <RTClib.h>
#include <LiquidCrystal_I2C.h>
#include <Adafruit_Fingerprint.h>

// ---------------------------
// WiFi Configuration
// ---------------------------
const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASS = "YOUR_WIFI_PASSWORD";

// ---------------------------
// Hardware Pins
// ---------------------------
#define GREEN_LED 25
#define RED_LED   26
#define BUZZER    27

// Fingerprint sensor uses UART2: GPIO16 RX2, GPIO17 TX2
HardwareSerial mySerial(2);
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&mySerial);

// RTC and LCD share I2C bus
RTC_DS3231 rtc;
LiquidCrystal_I2C lcd(0x27, 16, 4);

WebServer server(80);

String currentTeacherId = "";
String activeLectureId = "";
bool attendanceActive = false;
String currentSessionTeacherId = "";

// ---------------------------
// Data Helpers
// ---------------------------
String getCurrentDateTimeString() {
  if (!rtc.begin()) {
    return "RTC ERROR";
  }

  DateTime now = rtc.now();
  char dt[30];
  snprintf(dt, sizeof(dt), "%04d-%02d-%02d %02d:%02d:%02d",
           now.year(), now.month(), now.day(),
           now.hour(), now.minute(), now.second());
  return String(dt);
}

String getCurrentDateString() {
  if (!rtc.begin()) return "RTC ERROR";
  DateTime now = rtc.now();
  char d[20];
  snprintf(d, sizeof(d), "%04d-%02d-%02d", now.year(), now.month(), now.day());
  return String(d);
}

String getCurrentTimeString() {
  if (!rtc.begin()) return "RTC ERROR";
  DateTime now = rtc.now();
  char t[20];
  snprintf(t, sizeof(t), "%02d:%02d:%02d", now.hour(), now.minute(), now.second());
  return String(t);
}

void beep(int times, int delayMs = 150) {
  for (int i = 0; i < times; i++) {
    digitalWrite(BUZZER, HIGH);
    delay(delayMs);
    digitalWrite(BUZZER, LOW);
    delay(80);
  }
}

void setLed(bool green, bool red) {
  digitalWrite(GREEN_LED, green ? HIGH : LOW);
  digitalWrite(RED_LED, red ? HIGH : LOW);
}

void showLCD(String line1, String line2, String line3, String line4) {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print(line1.substring(0, 16));
  lcd.setCursor(0, 1); lcd.print(line2.substring(0, 16));
  lcd.setCursor(0, 2); lcd.print(line3.substring(0, 16));
  lcd.setCursor(0, 3); lcd.print(line4.substring(0, 16));
}

void showNormalLCD() {
  String time = getCurrentTimeString();
  String date = getCurrentDateString();

  if (WiFi.status() == WL_CONNECTED) {
    showLCD("WiFi Connected", WiFi.localIP().toString(), date, time);
  } else {
    showLCD("WiFi Not Connected", "Connecting...", date, time);
  }
}

bool fileExists(const char* path) {
  return LittleFS.exists(path);
}

void ensureFile(const char* path, const char* defaultJson) {
  if (!fileExists(path)) {
    File f = LittleFS.open(path, "w");
    if (f) {
      f.print(defaultJson);
      f.close();
    }
  }
}

String readFileString(const char* path) {
  File f = LittleFS.open(path, "r");
  if (!f) return "[]";
  String out = f.readString();
  f.close();
  return out;
}

JsonDocument loadJson(const char* path, const char* fallback) {
  JsonDocument doc;
  String payload = readFileString(path);
  if (payload.length() == 0) payload = fallback;
  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    doc.clear();
    deserializeJson(doc, fallback);
  }
  return doc;
}

bool saveJson(const char* path, JsonDocument& doc) {
  File f = LittleFS.open(path, "w");
  if (!f) return false;
  serializeJson(doc, f);
  f.close();
  return true;
}

// ---------------------------
// Default Data Initialization
// ---------------------------
void initStorage() {
  LittleFS.begin(true);
  ensureFile("/teachers.json", "[]");
  ensureFile("/students.json", "[]");
  ensureFile("/subjects.json", "[]");
  ensureFile("/schedules.json", "[]");
  ensureFile("/attendance.json", "[]");
  ensureFile("/settings.json", "{\"adminId\":\"ADMIN\",\"adminPass\":\"admin123\"}");
}

void createDefaultAdminIfNeeded() {
  JsonDocument settings = loadJson("/settings.json", "{\"adminId\":\"ADMIN\",\"adminPass\":\"admin123\"}");
  if (!settings.containsKey("adminId")) settings["adminId"] = "ADMIN";
  if (!settings.containsKey("adminPass")) settings["adminPass"] = "admin123";
  saveJson("/settings.json", settings);
}

// ---------------------------
// Teacher / Student / Subject / Schedule JSON Access
// ---------------------------
JsonArray getTeacherArray() {
  JsonDocument doc = loadJson("/teachers.json", "[]");
  JsonArray arr = doc.to<JsonArray>();
  saveJson("/teachers.json", doc);
  return arr;
}

JsonArray getStudentArray() {
  JsonDocument doc = loadJson("/students.json", "[]");
  JsonArray arr = doc.to<JsonArray>();
  saveJson("/students.json", doc);
  return arr;
}

JsonArray getSubjectArray() {
  JsonDocument doc = loadJson("/subjects.json", "[]");
  JsonArray arr = doc.to<JsonArray>();
  saveJson("/subjects.json", doc);
  return arr;
}

JsonArray getScheduleArray() {
  JsonDocument doc = loadJson("/schedules.json", "[]");
  JsonArray arr = doc.to<JsonArray>();
  saveJson("/schedules.json", doc);
  return arr;
}

JsonArray getAttendanceArray() {
  JsonDocument doc = loadJson("/attendance.json", "[]");
  JsonArray arr = doc.to<JsonArray>();
  saveJson("/attendance.json", doc);
  return arr;
}

String toJsonString(JsonDocument& doc) {
  String out;
  serializeJsonPretty(doc, out);
  return out;
}

String findTeacherById(String teacherId) {
  JsonDocument doc = loadJson("/teachers.json", "[]");
  JsonArray arr = doc.as<JsonArray>();
  for (JsonVariant v : arr) {
    if (v["teacherId"].as<String>() == teacherId) {
      return v["name"].as<String>();
    }
  }
  return "";
}

String findStudentByFingerprint(int id) {
  JsonDocument doc = loadJson("/students.json", "[]");
  JsonArray arr = doc.as<JsonArray>();
  for (JsonVariant v : arr) {
    if (v["fingerprintId"].as<int>() == id) {
      return v["name"].as<String>();
    }
  }
  return "";
}

String findStudentNameById(String studentId) {
  JsonDocument doc = loadJson("/students.json", "[]");
  JsonArray arr = doc.as<JsonArray>();
  for (JsonVariant v : arr) {
    if (v["studentId"].as<String>() == studentId) {
      return v["name"].as<String>();
    }
  }
  return "";
}

String findStudentById(String studentId) {
  JsonDocument doc = loadJson("/students.json", "[]");
  JsonArray arr = doc.as<JsonArray>();
  for (JsonVariant v : arr) {
    if (v["studentId"].as<String>() == studentId) {
      String out;
      serializeJson(v, out);
      return out;
    }
  }
  return "{}";
}

String getStudentFromFingerprint(int fingerprintId) {
  JsonDocument doc = loadJson("/students.json", "[]");
  JsonArray arr = doc.as<JsonArray>();
  for (JsonVariant v : arr) {
    if (v["fingerprintId"].as<int>() == fingerprintId) {
      String out;
      serializeJson(v, out);
      return out;
    }
  }
  return "{}";
}

bool isStudentAlreadyPresent(String studentId, String lectureId) {
  JsonDocument doc = loadJson("/attendance.json", "[]");
  JsonArray arr = doc.as<JsonArray>();
  for (JsonVariant v : arr) {
    if (v["lectureId"].as<String>() == lectureId && v["studentId"].as<String>() == studentId && v["status"].as<String>() == "present") {
      return true;
    }
  }
  return false;
}

String getTeacherFromId(String teacherId) {
  JsonDocument doc = loadJson("/teachers.json", "[]");
  JsonArray arr = doc.as<JsonArray>();
  for (JsonVariant v : arr) {
    if (v["teacherId"].as<String>() == teacherId) {
      String out;
      serializeJson(v, out);
      return out;
    }
  }
  return "{}";
}

String getLectureById(String lectureId) {
  JsonDocument doc = loadJson("/schedules.json", "[]");
  JsonArray arr = doc.as<JsonArray>();
  for (JsonVariant v : arr) {
    if (v["id"].as<String>() == lectureId) {
      String out;
      serializeJson(v, out);
      return out;
    }
  }
  return "{}";
}

String getSubjectByCode(String code) {
  JsonDocument doc = loadJson("/subjects.json", "[]");
  JsonArray arr = doc.as<JsonArray>();
  for (JsonVariant v : arr) {
    if (v["code"].as<String>() == code) {
      String out;
      serializeJson(v, out);
      return out;
    }
  }
  return "{}";
}

// ---------------------------
// Fingerprint Enrollment & Matching
// ---------------------------
uint8_t getFingerprintEnrollId() {
  uint8_t id = 1;
  JsonDocument doc = loadJson("/students.json", "[]");
  JsonArray arr = doc.as<JsonArray>();
  for (JsonVariant v : arr) {
    int fid = v["fingerprintId"].as<int>();
    if (fid >= id) id = fid + 1;
  }
  return id;
}

uint8_t getFingerprintEnrollTeacherId() {
  uint8_t id = 1;
  JsonDocument doc = loadJson("/teachers.json", "[]");
  JsonArray arr = doc.as<JsonArray>();
  for (JsonVariant v : arr) {
    int fid = v["fingerprintId"].as<int>();
    if (fid >= id) id = fid + 1;
  }
  return id;
}

bool enrollFingerprintToDevice(uint8_t id) {
  uint8_t p = finger.emptyDatabase();
  if (p != FINGERPRINT_OK) {
    Serial.println("Empty database failed");
    return false;
  }

  Serial.println("Place finger to enroll...");
  while (1) {
    p = finger.getImage();
    if (p == FINGERPRINT_OK) break;
    delay(50);
  }

  p = finger.image2Tz(1);
  if (p != FINGERPRINT_OK) return false;

  Serial.println("Remove finger");
  delay(2000);

  while (1) {
    p = finger.getImage();
    if (p == FINGERPRINT_NOFINGER) continue;
    if (p == FINGERPRINT_OK) break;
  }

  p = finger.image2Tz(2);
  if (p != FINGERPRINT_OK) return false;

  p = finger.createModel();
  if (p != FINGERPRINT_OK) return false;

  p = finger.storeModel(id);
  if (p != FINGERPRINT_OK) return false;

  return true;
}

int scanFingerprint() {
  uint8_t p = finger.getImage();
  if (p != FINGERPRINT_OK) return -1;

  p = finger.image2Tz();
  if (p != FINGERPRINT_OK) return -1;

  p = finger.fingerFastSearch();
  if (p != FINGERPRINT_OK) return -1;

  return finger.fingerID;
}

// ---------------------------
// Web API
// ---------------------------
String jsonSuccess(String message) {
  JsonDocument doc;
  doc["success"] = true;
  doc["message"] = message;
  String out;
  serializeJson(doc, out);
  return out;
}

String jsonError(String message) {
  JsonDocument doc;
  doc["success"] = false;
  doc["message"] = message;
  String out;
  serializeJson(doc, out);
  return out;
}

String dashboardHtml() {
  return R"HTML(
<!doctype html>
<html lang="en">
<head>
  <meta charset="UTF-8"/>
  <meta name="viewport" content="width=device-width, initial-scale=1.0"/>
  <title>ESP32 Smart Attendance</title>
  <link rel="stylesheet" href="https://cdn.jsdelivr.net/npm/bootstrap@5.3.3/dist/css/bootstrap.min.css">
  <style>
    body { background:#111827; color:#e5e7eb; }
    .navbar { background:#0f172a; }
    .card { background:#1f2937; border:1px solid #374151; }
    .section { display:none; }
    .active { display:block; }
    .badge { font-size: 0.8rem; }
    .form-control, .form-select { background:#0f172a; color:#fff; border:1px solid #374151; }
    .btn-primary { background:#2563eb; border:none; }
    .btn-success { background:#16a34a; border:none; }
    .btn-danger { background:#dc2626; border:none; }
    .table { color:#e5e7eb; }
    .nav-link { color:#dbeafe !important; }
    .top-status { font-size: 0.9rem; }
  </style>
</head>
<body>
  <nav class="navbar navbar-expand-lg navbar-dark">
    <div class="container-fluid px-4">
      <a class="navbar-brand" href="#">Smart Attendance</a>
      <button class="navbar-toggler" type="button" data-bs-toggle="collapse" data-bs-target="#navMenu">
        <span class="navbar-toggler-icon"></span>
      </button>
      <div class="collapse navbar-collapse" id="navMenu">
        <ul class="navbar-nav me-auto mb-2 mb-lg-0">
          <li class="nav-item"><a class="nav-link" href="#" onclick="showSection('home')">Home</a></li>
          <li class="nav-item"><a class="nav-link" href="#" onclick="showSection('teacher-dashboard')">Teacher Dashboard</a></li>
          <li class="nav-item"><a class="nav-link" href="#" onclick="showSection('students')">Student Management</a></li>
          <li class="nav-item"><a class="nav-link" href="#" onclick="showSection('teachers')">Teacher Management</a></li>
          <li class="nav-item"><a class="nav-link" href="#" onclick="showSection('subjects')">Subjects</a></li>
          <li class="nav-item"><a class="nav-link" href="#" onclick="showSection('schedules')">Lecture Scheduler</a></li>
          <li class="nav-item"><a class="nav-link" href="#" onclick="showSection('attendance')">Attendance</a></li>
          <li class="nav-item"><a class="nav-link" href="#" onclick="showSection('reports')">Reports</a></li>
          <li class="nav-item"><a class="nav-link" href="#" onclick="showSection('settings')">Settings</a></li>
        </ul>
        <button class="btn btn-outline-light btn-sm" onclick="logout()">Logout</button>
      </div>
    </div>
  </nav>

  <div class="container mt-4">
    <div class="row mb-3">
      <div class="col" id="statusBar">Loading...</div>
    </div>

    <section id="home" class="section active">
      <div class="row g-3">
        <div class="col-md-3"><div class="card p-3"><h6>Total Students</h6><h3 id="totalStudents">0</h3></div></div>
        <div class="col-md-3"><div class="card p-3"><h6>Total Teachers</h6><h3 id="totalTeachers">0</h3></div></div>
        <div class="col-md-3"><div class="card p-3"><h6>Present</h6><h3 id="presentCount">0</h3></div></div>
        <div class="col-md-3"><div class="card p-3"><h6>Absent</h6><h3 id="absentCount">0</h3></div></div>
      </div>
      <div class="row mt-4">
        <div class="col-lg-6">
          <div class="card p-3">
            <h5>Login</h5>
            <form id="loginForm">
              <div class="mb-2">
                <label class="form-label">Teacher ID / Admin ID</label>
                <input class="form-control" id="loginId" required>
              </div>
              <div class="mb-2">
                <label class="form-label">Password / PIN</label>
                <input type="password" class="form-control" id="loginPass" required>
              </div>
              <button class="btn btn-primary w-100" type="submit">Login</button>
            </form>
          </div>
        </div>
        <div class="col-lg-6">
          <div class="card p-3">
            <h5>System Status</h5>
            <div id="systemStatus">Checking...</div>
          </div>
        </div>
      </div>
    </section>

    <section id="teacher-dashboard" class="section">
      <div class="card p-3">
        <h5>Teacher Dashboard</h5>
        <div id="teacherDashboardContent"></div>
      </div>
    </section>

    <section id="students" class="section">
      <div class="card p-3">
        <h5>Student Management</h5>
        <form id="studentForm" class="row g-2">
          <input type="hidden" id="studentIdHidden">
          <div class="col-md-4"><input class="form-control" id="studentName" placeholder="Student Name"></div>
          <div class="col-md-4"><input class="form-control" id="studentRoll" placeholder="Roll Number"></div>
          <div class="col-md-4"><input class="form-control" id="studentPrn" placeholder="PRN"></div>
          <div class="col-md-3"><input class="form-control" id="studentDepartment" placeholder="Department"></div>
          <div class="col-md-2"><input class="form-control" id="studentYear" placeholder="Year"></div>
          <div class="col-md-2"><input class="form-control" id="studentSemester" placeholder="Semester"></div>
          <div class="col-md-2"><input class="form-control" id="studentDivision" placeholder="Division"></div>
          <div class="col-md-3"><input class="form-control" id="studentBatch" placeholder="Batch"></div>
          <div class="col-md-4"><input class="form-control" id="studentEmail" placeholder="Email"></div>
          <div class="col-md-4"><input class="form-control" id="studentPhone" placeholder="Phone"></div>
          <div class="col-md-4"><input class="form-control" id="studentFinger" placeholder="Fingerprint ID"></div>
          <div class="col-md-12 text-end">
            <button class="btn btn-primary" type="submit">Save Student</button>
            <button class="btn btn-success" type="button" onclick="enrollStudentFingerprint()">Enroll Fingerprint</button>
          </div>
        </form>
        <div class="mt-3"><table class="table table-dark table-striped"><tbody id="studentTable"></tbody></table></div>
      </div>
    </section>

    <section id="teachers" class="section">
      <div class="card p-3">
        <h5>Teacher Management</h5>
        <form id="teacherForm" class="row g-2">
          <input type="hidden" id="teacherIdHidden">
          <div class="col-md-3"><input class="form-control" id="teacherName" placeholder="Teacher Name"></div>
          <div class="col-md-3"><input class="form-control" id="teacherIdField" placeholder="Teacher ID"></div>
          <div class="col-md-3"><input class="form-control" id="teacherEmail" placeholder="Email"></div>
          <div class="col-md-3"><input class="form-control" id="teacherPhone" placeholder="Phone"></div>
          <div class="col-md-3"><input class="form-control" id="teacherDepartment" placeholder="Department"></div>
          <div class="col-md-2"><input class="form-control" id="teacherPin" placeholder="PIN"></div>
          <div class="col-md-2"><input class="form-control" id="teacherPassword" placeholder="Password"></div>
          <div class="col-md-3"><input class="form-control" id="teacherFinger" placeholder="Fingerprint ID"></div>
          <div class="col-md-12 text-end">
            <button class="btn btn-primary" type="submit">Save Teacher</button>
            <button class="btn btn-success" type="button" onclick="enrollTeacherFingerprint()">Enroll Fingerprint</button>
          </div>
        </form>
        <div class="mt-3"><table class="table table-dark table-striped"><tbody id="teacherTable"></tbody></table></div>
      </div>
    </section>

    <section id="subjects" class="section">
      <div class="card p-3">
        <h5>Subjects</h5>
        <form id="subjectForm" class="row g-2">
          <div class="col-md-3"><input class="form-control" id="subjectCode" placeholder="Subject Code"></div>
          <div class="col-md-3"><input class="form-control" id="subjectName" placeholder="Subject Name"></div>
          <div class="col-md-3"><input class="form-control" id="subjectTeacher" placeholder="Teacher ID"></div>
          <div class="col-md-3"><input class="form-control" id="subjectDepartment" placeholder="Department"></div>
          <div class="col-md-3"><input class="form-control" id="subjectSemester" placeholder="Semester"></div>
          <div class="col-md-3"><input class="form-control" id="subjectDivision" placeholder="Division"></div>
          <div class="col-md-3"><input class="form-control" id="subjectBatch" placeholder="Batch"></div>
          <div class="col-md-3 text-end"><button class="btn btn-primary w-100" type="submit">Add Subject</button></div>
        </form>
        <div class="mt-3"><table class="table table-dark table-striped"><tbody id="subjectTable"></tbody></table></div>
      </div>
    </section>

    <section id="schedules" class="section">
      <div class="card p-3">
        <h5>Lecture Scheduler</h5>
        <form id="scheduleForm" class="row g-2">
          <div class="col-md-3"><input class="form-control" id="scheduleSubject" placeholder="Subject Code"></div>
          <div class="col-md-3"><input class="form-control" id="scheduleTeacher" placeholder="Teacher ID"></div>
          <div class="col-md-2"><input class="form-control" id="scheduleDepartment" placeholder="Department"></div>
          <div class="col-md-2"><input class="form-control" id="scheduleSemester" placeholder="Semester"></div>
          <div class="col-md-2"><input class="form-control" id="scheduleDivision" placeholder="Division"></div>
          <div class="col-md-2"><input class="form-control" id="scheduleBatch" placeholder="Batch"></div>
          <div class="col-md-2"><input class="form-control" id="scheduleDay" placeholder="Day"></div>
          <div class="col-md-2"><input class="form-control" id="scheduleStart" placeholder="Start Time"></div>
          <div class="col-md-2"><input class="form-control" id="scheduleEnd" placeholder="End Time"></div>
          <div class="col-md-2"><input class="form-control" id="scheduleRoom" placeholder="Room"></div>
          <div class="col-md-2 text-end"><button class="btn btn-primary w-100" type="submit">Save Schedule</button></div>
        </form>
        <div class="mt-3"><table class="table table-dark table-striped"><tbody id="scheduleTable"></tbody></table></div>
      </div>
    </section>

    <section id="attendance" class="section">
      <div class="card p-3">
        <h5>Attendance Process</h5>
        <div class="row g-2">
          <div class="col-md-4"><input class="form-control" id="lectureSelect" placeholder="Lecture ID"></div>
          <div class="col-md-4"><input class="form-control" id="teacherPinInput" type="password" placeholder="Teacher PIN"></div>
          <div class="col-md-4">
            <button class="btn btn-success w-100" onclick="startAttendanceSession()">Start Attendance</button>
          </div>
        </div>
        <div class="mt-3 d-flex gap-2">
          <button class="btn btn-danger" onclick="endAttendanceSession()">End Attendance</button>
          <button class="btn btn-warning" onclick="refreshAttendanceStatus()">Refresh</button>
        </div>
        <div class="mt-3"><div id="attendanceStatusBox">No active lecture.</div></div>
      </div>
    </section>

    <section id="reports" class="section">
      <div class="card p-3">
        <h5>Reports</h5>
        <button class="btn btn-primary" onclick="loadReports()">Generate Reports</button>
        <div class="mt-3" id="reportsContent"></div>
      </div>
    </section>

    <section id="settings" class="section">
      <div class="card p-3">
        <h5>Settings</h5>
        <div class="mb-2"><label>WiFi SSID</label><input class="form-control" id="wifiSsid" value="YOUR_WIFI_SSID"></div>
        <div class="mb-2"><label>WiFi Password</label><input type="password" class="form-control" id="wifiPass" value="YOUR_WIFI_PASSWORD"></div>
        <button class="btn btn-primary" onclick="saveSettings()">Save</button>
      </div>
    </section>
  </div>

  <script src="https://cdn.jsdelivr.net/npm/bootstrap@5.3.3/dist/js/bootstrap.bundle.min.js"></script>
  <script>
    function showSection(id) {
      document.querySelectorAll('.section').forEach(s => s.classList.remove('active'));
      document.getElementById(id).classList.add('active');
    }

    async function api(url, method='GET', body=null) {
      const options = { method, headers: {'Content-Type':'application/json'} };
      if (body) options.body = JSON.stringify(body);
      const res = await fetch(url, options);
      return await res.json();
    }

    async function loadSummary() {
      const data = await api('/api/dashboard');
      if (data && data.totalStudents !== undefined) {
        document.getElementById('totalStudents').innerText = data.totalStudents;
        document.getElementById('totalTeachers').innerText = data.totalTeachers;
        document.getElementById('presentCount').innerText = data.present;
        document.getElementById('absentCount').innerText = data.absent;
      }
    }

    async function loadTeacherDashboard() {
      const data = await api('/api/teacher-dashboard');
      document.getElementById('teacherDashboardContent').innerHTML = data.html || 'No dashboard data';
    }

    async function loadTables() {
      const teachers = await api('/api/teachers');
      const students = await api('/api/students');
      const subjects = await api('/api/subjects');
      const schedules = await api('/api/schedules');

      const teacherRows = teachers.map(t => `<tr><td>${t.name}</td><td>${t.teacherId}</td><td>${t.department}</td><td>${t.email}</td><td><button class="btn btn-sm btn-danger" onclick="deleteTeacher('${t.teacherId}')">Delete</button></td></tr>`).join('');
      const studentRows = students.map(s => `<tr><td>${s.name}</td><td>${s.rollNumber}</td><td>${s.prn}</td><td>${s.batch}</td><td>${s.division}</td><td><button class="btn btn-sm btn-danger" onclick="deleteStudent('${s.studentId}')">Delete</button></td></tr>`).join('');
      const subjectRows = subjects.map(s => `<tr><td>${s.code}</td><td>${s.name}</td><td>${s.teacherId}</td><td>${s.department}</td><td><button class="btn btn-sm btn-danger" onclick="deleteSubject('${s.code}')">Delete</button></td></tr>`).join('');
      const scheduleRows = schedules.map(s => `<tr><td>${s.subjectCode}</td><td>${s.teacherId}</td><td>${s.day}</td><td>${s.startTime}-${s.endTime}</td><td>${s.roomNumber}</td><td><button class="btn btn-sm btn-danger" onclick="deleteSchedule('${s.id}')">Delete</button></td></tr>`).join('');

      document.getElementById('teacherTable').innerHTML = teacherRows;
      document.getElementById('studentTable').innerHTML = studentRows;
      document.getElementById('subjectTable').innerHTML = subjectRows;
      document.getElementById('scheduleTable').innerHTML = scheduleRows;
    }

    async function loadReports() {
      const data = await api('/api/reports');
      document.getElementById('reportsContent').innerHTML = data.html || 'No reports yet.';
    }

    document.getElementById('loginForm').addEventListener('submit', async e => {
      e.preventDefault();
      const id = document.getElementById('loginId').value;
      const pass = document.getElementById('loginPass').value;
      const res = await api('/api/login', 'POST', { id, pass });
      alert(res.message || 'Login result');
      if (res.success) {
        loadSummary();
        loadTeacherDashboard();
        loadTables();
      }
    });

    document.getElementById('studentForm').addEventListener('submit', async e => {
      e.preventDefault();
      const payload = {
        studentId: document.getElementById('studentIdHidden').value || Math.random().toString(36).slice(2, 9),
        name: document.getElementById('studentName').value,
        rollNumber: document.getElementById('studentRoll').value,
        prn: document.getElementById('studentPrn').value,
        department: document.getElementById('studentDepartment').value,
        year: document.getElementById('studentYear').value,
        semester: document.getElementById('studentSemester').value,
        division: document.getElementById('studentDivision').value,
        batch: document.getElementById('studentBatch').value,
        email: document.getElementById('studentEmail').value,
        phone: document.getElementById('studentPhone').value,
        fingerprintId: Number(document.getElementById('studentFinger').value || 0)
      };
      const res = await api('/api/students', 'POST', payload);
      alert(res.message || 'Saved');
      loadTables();
    });

    document.getElementById('teacherForm').addEventListener('submit', async e => {
      e.preventDefault();
      const payload = {
        teacherId: document.getElementById('teacherIdField').value,
        name: document.getElementById('teacherName').value,
        email: document.getElementById('teacherEmail').value,
        phone: document.getElementById('teacherPhone').value,
        department: document.getElementById('teacherDepartment').value,
        pin: document.getElementById('teacherPin').value,
        password: document.getElementById('teacherPassword').value,
        fingerprintId: Number(document.getElementById('teacherFinger').value || 0)
      };
      const res = await api('/api/teachers', 'POST', payload);
      alert(res.message || 'Saved');
      loadTables();
    });

    document.getElementById('subjectForm').addEventListener('submit', async e => {
      e.preventDefault();
      const payload = {
        code: document.getElementById('subjectCode').value,
        name: document.getElementById('subjectName').value,
        teacherId: document.getElementById('subjectTeacher').value,
        department: document.getElementById('subjectDepartment').value,
        semester: document.getElementById('subjectSemester').value,
        division: document.getElementById('subjectDivision').value,
        batch: document.getElementById('subjectBatch').value
      };
      const res = await api('/api/subjects', 'POST', payload);
      alert(res.message || 'Saved');
      loadTables();
    });

    document.getElementById('scheduleForm').addEventListener('submit', async e => {
      e.preventDefault();
      const payload = {
        id: 'sch_' + Date.now(),
        subjectCode: document.getElementById('scheduleSubject').value,
        teacherId: document.getElementById('scheduleTeacher').value,
        department: document.getElementById('scheduleDepartment').value,
        semester: document.getElementById('scheduleSemester').value,
        division: document.getElementById('scheduleDivision').value,
        batch: document.getElementById('scheduleBatch').value,
        day: document.getElementById('scheduleDay').value,
        startTime: document.getElementById('scheduleStart').value,
        endTime: document.getElementById('scheduleEnd').value,
        roomNumber: document.getElementById('scheduleRoom').value
      };
      const res = await api('/api/schedules', 'POST', payload);
      alert(res.message || 'Saved');
      loadTables();
    });

    async function startAttendanceSession() {
      const lectureId = document.getElementById('lectureSelect').value;
      const pin = document.getElementById('teacherPinInput').value;
      const res = await api('/api/attendance/start', 'POST', { lectureId, pin });
      alert(res.message || 'Session status');
      refreshAttendanceStatus();
    }

    async function endAttendanceSession() {
      const res = await api('/api/attendance/end', 'POST', {});
      alert(res.message || 'Session ended');
      refreshAttendanceStatus();
    }

    async function refreshAttendanceStatus() {
      const res = await api('/api/attendance/status', 'GET');
      document.getElementById('attendanceStatusBox').innerHTML = res.html || res.message || 'No active lecture';
    }

    async function deleteTeacher(id) {
      const res = await api('/api/teachers', 'DELETE', { teacherId: id });
      alert(res.message || 'Deleted');
      loadTables();
    }

    async function deleteStudent(id) {
      const res = await api('/api/students', 'DELETE', { studentId: id });
      alert(res.message || 'Deleted');
      loadTables();
    }

    async function deleteSubject(code) {
      const res = await api('/api/subjects', 'DELETE', { code });
      alert(res.message || 'Deleted');
      loadTables();
    }

    async function deleteSchedule(id) {
      const res = await api('/api/schedules', 'DELETE', { id });
      alert(res.message || 'Deleted');
      loadTables();
    }

    async function enrollStudentFingerprint() {
      const studentId = document.getElementById('studentIdHidden').value || document.getElementById('studentRoll').value;
      const res = await api('/api/fingerprint/enrollStudent', 'POST', { studentId });
      alert(res.message || 'Fingerprint enrolled');
    }

    async function enrollTeacherFingerprint() {
      const teacherId = document.getElementById('teacherIdField').value;
      const res = await api('/api/fingerprint/enrollTeacher', 'POST', { teacherId });
      alert(res.message || 'Fingerprint enrolled');
    }

    async function saveSettings() {
      const payload = {
        ssid: document.getElementById('wifiSsid').value,
        pass: document.getElementById('wifiPass').value
      };
      const res = await api('/api/settings', 'POST', payload);
      alert(res.message || 'Saved');
    }

    async function logout() {
      const res = await api('/api/logout', 'POST', {});
      alert(res.message || 'Logged out');
    }

    (function init(){
      loadSummary();
      loadTeacherDashboard();
      loadTables();
      refreshAttendanceStatus();
    })();
  </script>
</body>
</html>
)HTML";
}

// ---------------------------
// API Endpoints
// ---------------------------
void handleHome() {
  server.send(200, "text/html", dashboardHtml());
}

void handleDashboard() {
  JsonDocument doc;
  JsonDocument studentsDoc = loadJson("/students.json", "[]");
  JsonDocument teachersDoc = loadJson("/teachers.json", "[]");
  JsonArray students = studentsDoc.as<JsonArray>();
  JsonArray teachers = teachersDoc.as<JsonArray>();
  doc["totalStudents"] = students.size();
  doc["totalTeachers"] = teachers.size();
  doc["present"] = 0;
  doc["absent"] = 0;
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void handleLogin() {
  if (server.hasArg("plain") == false) {
    server.send(400, "application/json", jsonError("Missing request body"));
    return;
  }

  String body = server.arg("plain");
  JsonDocument doc;
  deserializeJson(doc, body);

  String id = doc["id"] | "";
  String pass = doc["pass"] | "";

  JsonDocument settings = loadJson("/settings.json", "{\"adminId\":\"ADMIN\",\"adminPass\":\"admin123\"}");
  String adminId = settings["adminId"].as<String>();
  String adminPass = settings["adminPass"].as<String>();

  if (id == adminId && pass == adminPass) {
    currentTeacherId = "ADMIN";
    server.send(200, "application/json", jsonSuccess("Admin login successful"));
    return;
  }

  JsonDocument teachers = loadJson("/teachers.json", "[]");
  JsonArray arr = teachers.as<JsonArray>();
  for (JsonVariant v : arr) {
    if ((v["teacherId"].as<String>() == id && v["password"].as<String>() == pass) ||
        (v["teacherId"].as<String>() == id && v["pin"].as<String>() == pass)) {
      currentTeacherId = id;
      server.send(200, "application/json", jsonSuccess("Teacher login successful"));
      return;
    }
  }

  server.send(401, "application/json", jsonError("Invalid credentials"));
}

void handleLogout() {
  currentTeacherId = "";
  server.send(200, "application/json", jsonSuccess("Logged out"));
}

void handleTeachers() {
  if (server.method() == HTTP_GET) {
    JsonDocument doc = loadJson("/teachers.json", "[]");
    String out;
    serializeJson(doc, out);
    server.send(200, "application/json", out);
    return;
  }

  if (server.method() == HTTP_POST) {
    String body = server.arg("plain");
    JsonDocument doc;
    deserializeJson(doc, body);
    JsonDocument all = loadJson("/teachers.json", "[]");
    JsonArray arr = all.as<JsonArray>();

    bool found = false;
    for (JsonVariant v : arr) {
      if (v["teacherId"].as<String>() == doc["teacherId"].as<String>()) {
        v["name"] = doc["name"] | "";
        v["email"] = doc["email"] | "";
        v["phone"] = doc["phone"] | "";
        v["department"] = doc["department"] | "";
        v["pin"] = doc["pin"] | "";
        v["password"] = doc["password"] | "";
        v["fingerprintId"] = doc["fingerprintId"] | 0;
        found = true;
        break;
      }
    }

    if (!found) {
      JsonObject obj = arr.add<JsonObject>();
      obj["teacherId"] = doc["teacherId"] | "";
      obj["name"] = doc["name"] | "";
      obj["email"] = doc["email"] | "";
      obj["phone"] = doc["phone"] | "";
      obj["department"] = doc["department"] | "";
      obj["pin"] = doc["pin"] | "";
      obj["password"] = doc["password"] | "";
      obj["fingerprintId"] = doc["fingerprintId"] | 0;
    }

    saveJson("/teachers.json", all);
    server.send(200, "application/json", jsonSuccess("Teacher saved"));
    return;
  }

  if (server.method() == HTTP_DELETE) {
    String body = server.arg("plain");
    JsonDocument doc;
    deserializeJson(doc, body);
    String teacherId = doc["teacherId"] | "";
    JsonDocument all = loadJson("/teachers.json", "[]");
    JsonArray arr = all.as<JsonArray>();
    for (int i = arr.size() - 1; i >= 0; i--) {
      if (arr[i]["teacherId"].as<String>() == teacherId) {
        arr.remove(i);
      }
    }
    saveJson("/teachers.json", all);
    server.send(200, "application/json", jsonSuccess("Teacher deleted"));
    return;
  }
}

void handleStudents() {
  if (server.method() == HTTP_GET) {
    JsonDocument doc = loadJson("/students.json", "[]");
    String out;
    serializeJson(doc, out);
    server.send(200, "application/json", out);
    return;
  }

  if (server.method() == HTTP_POST) {
    String body = server.arg("plain");
    JsonDocument doc;
    deserializeJson(doc, body);
    JsonDocument all = loadJson("/students.json", "[]");
    JsonArray arr = all.as<JsonArray>();

    bool found = false;
    for (JsonVariant v : arr) {
      if (v["studentId"].as<String>() == doc["studentId"].as<String>()) {
        v["name"] = doc["name"] | "";
        v["rollNumber"] = doc["rollNumber"] | "";
        v["prn"] = doc["prn"] | "";
        v["department"] = doc["department"] | "";
        v["year"] = doc["year"] | "";
        v["semester"] = doc["semester"] | "";
        v["division"] = doc["division"] | "";
        v["batch"] = doc["batch"] | "";
        v["email"] = doc["email"] | "";
        v["phone"] = doc["phone"] | "";
        v["fingerprintId"] = doc["fingerprintId"] | 0;
        found = true;
        break;
      }
    }

    if (!found) {
      JsonObject obj = arr.add<JsonObject>();
      obj["studentId"] = doc["studentId"] | "";
      obj["name"] = doc["name"] | "";
      obj["rollNumber"] = doc["rollNumber"] | "";
      obj["prn"] = doc["prn"] | "";
      obj["department"] = doc["department"] | "";
      obj["year"] = doc["year"] | "";
      obj["semester"] = doc["semester"] | "";
      obj["division"] = doc["division"] | "";
      obj["batch"] = doc["batch"] | "";
      obj["email"] = doc["email"] | "";
      obj["phone"] = doc["phone"] | "";
      obj["fingerprintId"] = doc["fingerprintId"] | 0;
    }

    saveJson("/students.json", all);
    server.send(200, "application/json", jsonSuccess("Student saved"));
    return;
  }

  if (server.method() == HTTP_DELETE) {
    String body = server.arg("plain");
    JsonDocument doc;
    deserializeJson(doc, body);
    String studentId = doc["studentId"] | "";
    JsonDocument all = loadJson("/students.json", "[]");
    JsonArray arr = all.as<JsonArray>();
    for (int i = arr.size() - 1; i >= 0; i--) {
      if (arr[i]["studentId"].as<String>() == studentId) {
        arr.remove(i);
      }
    }
    saveJson("/students.json", all);
    server.send(200, "application/json", jsonSuccess("Student deleted"));
    return;
  }
}

void handleSubjects() {
  if (server.method() == HTTP_GET) {
    JsonDocument doc = loadJson("/subjects.json", "[]");
    String out;
    serializeJson(doc, out);
    server.send(200, "application/json", out);
    return;
  }

  if (server.method() == HTTP_POST) {
    String body = server.arg("plain");
    JsonDocument doc;
    deserializeJson(doc, body);
    JsonDocument all = loadJson("/subjects.json", "[]");
    JsonArray arr = all.as<JsonArray>();
    JsonObject obj = arr.add<JsonObject>();
    obj["code"] = doc["code"] | "";
    obj["name"] = doc["name"] | "";
    obj["teacherId"] = doc["teacherId"] | "";
    obj["department"] = doc["department"] | "";
    obj["semester"] = doc["semester"] | "";
    obj["division"] = doc["division"] | "";
    obj["batch"] = doc["batch"] | "";
    saveJson("/subjects.json", all);
    server.send(200, "application/json", jsonSuccess("Subject saved"));
    return;
  }

  if (server.method() == HTTP_DELETE) {
    String body = server.arg("plain");
    JsonDocument doc;
    deserializeJson(doc, body);
    String code = doc["code"] | "";
    JsonDocument all = loadJson("/subjects.json", "[]");
    JsonArray arr = all.as<JsonArray>();
    for (int i = arr.size() - 1; i >= 0; i--) {
      if (arr[i]["code"].as<String>() == code) {
        arr.remove(i);
      }
    }
    saveJson("/subjects.json", all);
    server.send(200, "application/json", jsonSuccess("Subject deleted"));
    return;
  }
}

void handleSchedules() {
  if (server.method() == HTTP_GET) {
    JsonDocument doc = loadJson("/schedules.json", "[]");
    String out;
    serializeJson(doc, out);
    server.send(200, "application/json", out);
    return;
  }

  if (server.method() == HTTP_POST) {
    String body = server.arg("plain");
    JsonDocument doc;
    deserializeJson(doc, body);
    JsonDocument all = loadJson("/schedules.json", "[]");
    JsonArray arr = all.as<JsonArray>();
    JsonObject obj = arr.add<JsonObject>();
    obj["id"] = doc["id"] | "";
    obj["subjectCode"] = doc["subjectCode"] | "";
    obj["teacherId"] = doc["teacherId"] | "";
    obj["department"] = doc["department"] | "";
    obj["semester"] = doc["semester"] | "";
    obj["division"] = doc["division"] | "";
    obj["batch"] = doc["batch"] | "";
    obj["day"] = doc["day"] | "";
    obj["startTime"] = doc["startTime"] | "";
    obj["endTime"] = doc["endTime"] | "";
    obj["roomNumber"] = doc["roomNumber"] | "";
    saveJson("/schedules.json", all);
    server.send(200, "application/json", jsonSuccess("Schedule saved"));
    return;
  }

  if (server.method() == HTTP_DELETE) {
    String body = server.arg("plain");
    JsonDocument doc;
    deserializeJson(doc, body);
    String lectureId = doc["id"] | "";
    JsonDocument all = loadJson("/schedules.json", "[]");
    JsonArray arr = all.as<JsonArray>();
    for (int i = arr.size() - 1; i >= 0; i--) {
      if (arr[i]["id"].as<String>() == lectureId) {
        arr.remove(i);
      }
    }
    saveJson("/schedules.json", all);
    server.send(200, "application/json", jsonSuccess("Schedule deleted"));
    return;
  }
}

void handleTeacherDashboard() {
  String html = "<h5>Teacher Dashboard</h5>";
  JsonDocument schedules = loadJson("/schedules.json", "[]");
  JsonArray arr = schedules.as<JsonArray>();
  if (arr.size() == 0) {
    html += "<p>No lectures assigned.</p>";
  } else {
    html += "<table class='table table-sm table-dark'><tr><th>Lecture</th><th>Subject</th><th>Day</th><th>Time</th></tr>";
    for (JsonVariant v : arr) {
      if (v["teacherId"].as<String>() == currentTeacherId || currentTeacherId == "ADMIN") {
        html += "<tr><td>" + v["id"].as<String>() + "</td><td>" + v["subjectCode"].as<String>() + "</td><td>" + v["day"].as<String>() + "</td><td>" + v["startTime"].as<String>() + " - " + v["endTime"].as<String>() + "</td></tr>";
      }
    }
    html += "</table>";
  }
  JsonDocument out;
  out["html"] = html;
  String payload;
  serializeJson(out, payload);
  server.send(200, "application/json", payload);
}

void handleSettings() {
  if (server.method() == HTTP_POST) {
    String body = server.arg("plain");
    JsonDocument doc;
    deserializeJson(doc, body);
    JsonDocument settings = loadJson("/settings.json", "{\"adminId\":\"ADMIN\",\"adminPass\":\"admin123\"}");
    settings["ssid"] = doc["ssid"] | WIFI_SSID;
    settings["pass"] = doc["pass"] | WIFI_PASS;
    saveJson("/settings.json", settings);
    server.send(200, "application/json", jsonSuccess("Settings saved"));
    return;
  }
  server.send(200, "application/json", jsonSuccess("Settings endpoint"));
}

void handleReports() {
  JsonDocument students = loadJson("/students.json", "[]");
  JsonDocument attendance = loadJson("/attendance.json", "[]");
  String html = "<h6>Daily Attendance Summary</h6><table class='table table-sm table-dark'><tr><th>Lecture</th><th>Present</th><th>Absent</th></tr>";

  JsonArray attend = attendance.as<JsonArray>();
  int present = 0;
  for (JsonVariant v : attend) {
    if (v["status"].as<String>() == "present") present++;
  }

  html += "<tr><td>Current</td><td>" + String(present) + "</td><td>" + String(students.as<JsonArray>().size() - present) + "</td></tr>";
  html += "</table>";

  JsonDocument out;
  out["html"] = html;
  String payload;
  serializeJson(out, payload);
  server.send(200, "application/json", payload);
}

void handleFingerprintEnrollStudent() {
  if (server.hasArg("plain") == false) {
    server.send(400, "application/json", jsonError("Missing studentId"));
    return;
  }

  String body = server.arg("plain");
  JsonDocument doc;
  deserializeJson(doc, body);
  String studentId = doc["studentId"] | "";

  uint8_t newId = getFingerprintEnrollId();
  Serial.println("Place finger to enroll student...");
  uint8_t p = finger.getImage();
  if (p != FINGERPRINT_OK) {
    server.send(400, "application/json", jsonError("No finger detected"));
    return;
  }

  p = finger.image2Tz(1);
  if (p != FINGERPRINT_OK) {server.send(400, "application/json", jsonError("Image error")); return;}
  delay(2000);
  p = finger.getImage();
  if (p == FINGERPRINT_OK) {
    p = finger.image2Tz(2);
    if (p != FINGERPRINT_OK) {server.send(400, "application/json", jsonError("Second image error")); return;}
  }
  p = finger.createModel();
  if (p != FINGERPRINT_OK) {server.send(400, "application/json", jsonError("Create model error")); return;}
  p = finger.storeModel(newId);
  if (p != FINGERPRINT_OK) {server.send(400, "application/json", jsonError("Store model error")); return;}

  JsonDocument students = loadJson("/students.json", "[]");
  JsonArray arr = students.as<JsonArray>();
  for (JsonVariant v : arr) {
    if (v["studentId"].as<String>() == studentId) {
      v["fingerprintId"] = newId;
      break;
    }
  }
  saveJson("/students.json", students);
  server.send(200, "application/json", jsonSuccess("Student fingerprint enrolled with ID " + String(newId)));
}

void handleFingerprintEnrollTeacher() {
  if (server.hasArg("plain") == false) {
    server.send(400, "application/json", jsonError("Missing teacherId"));
    return;
  }

  String body = server.arg("plain");
  JsonDocument doc;
  deserializeJson(doc, body);
  String teacherId = doc["teacherId"] | "";

  uint8_t newId = getFingerprintEnrollTeacherId();
  uint8_t p = finger.getImage();
  if (p != FINGERPRINT_OK) {
    server.send(400, "application/json", jsonError("No finger detected"));
    return;
  }

  p = finger.image2Tz(1);
  if (p != FINGERPRINT_OK) {server.send(400, "application/json", jsonError("Image error")); return;}
  delay(2000);
  p = finger.getImage();
  if (p == FINGERPRINT_OK) {
    p = finger.image2Tz(2);
    if (p != FINGERPRINT_OK) {server.send(400, "application/json", jsonError("Second image error")); return;}
  }
  p = finger.createModel();
  if (p != FINGERPRINT_OK) {server.send(400, "application/json", jsonError("Create model error")); return;}
  p = finger.storeModel(newId);
  if (p != FINGERPRINT_OK) {server.send(400, "application/json", jsonError("Store model error")); return;}

  JsonDocument teachers = loadJson("/teachers.json", "[]");
  JsonArray arr = teachers.as<JsonArray>();
  for (JsonVariant v : arr) {
    if (v["teacherId"].as<String>() == teacherId) {
      v["fingerprintId"] = newId;
      break;
    }
  }
  saveJson("/teachers.json", teachers);
  server.send(200, "application/json", jsonSuccess("Teacher fingerprint enrolled with ID " + String(newId)));
}

void handleAttendanceStart() {
  if (server.hasArg("plain") == false) {
    server.send(400, "application/json", jsonError("Missing data"));
    return;
  }

  String body = server.arg("plain");
  JsonDocument doc;
  deserializeJson(doc, body);
  String lectureId = doc["lectureId"] | "";
  String pin = doc["pin"] | "";

  if (lectureId == "") {
    server.send(400, "application/json", jsonError("Lecture ID required"));
    return;
  }

  String lectureJson = getLectureById(lectureId);
  JsonDocument lecture;
  deserializeJson(lecture, lectureJson);

  if (lecture.isNull()) {
    server.send(400, "application/json", jsonError("Lecture not found"));
    return;
  }

  JsonDocument teachers = loadJson("/teachers.json", "[]");
  bool teacherValid = false;
  for (JsonVariant v : teachers.as<JsonArray>()) {
    if (v["teacherId"].as<String>() == currentTeacherId && v["pin"].as<String>() == pin) {
      teacherValid = true;
    }
  }

  if (!teacherValid && currentTeacherId != "ADMIN") {
    server.send(401, "application/json", jsonError("Teacher PIN invalid"));
    return;
  }

  activeLectureId = lectureId;
  attendanceActive = true;
  currentSessionTeacherId = currentTeacherId;
  server.send(200, "application/json", jsonSuccess("Attendance session started for lecture " + lectureId));
}

void handleAttendanceStatus() {
  JsonDocument out;
  out["active"] = attendanceActive;
  out["lectureId"] = activeLectureId;
  out["message"] = attendanceActive ? "Attendance session active" : "No active lecture";
  String html = attendanceActive ? "<b>Lecture:</b> " + activeLectureId + "<br><b>Status:</b> Running" : "<b>No active lecture.</b>";
  out["html"] = html;
  String payload;
  serializeJson(out, payload);
  server.send(200, "application/json", payload);
}

void handleAttendanceEnd() {
  attendanceActive = false;
  String lecture = getLectureById(activeLectureId);
  JsonDocument lectureObj;
  deserializeJson(lectureObj, lecture);

  JsonDocument students = loadJson("/students.json", "[]");
  JsonArray studentList = students.as<JsonArray>();
  JsonDocument attendance = loadJson("/attendance.json", "[]");
  JsonArray attList = attendance.as<JsonArray>();

  for (JsonVariant s : studentList) {
    String sid = s["studentId"].as<String>();
    bool already = false;
    for (JsonVariant p : attList) {
      if (p["lectureId"].as<String>() == activeLectureId && p["studentId"].as<String>() == sid && p["status"].as<String>() == "present") {
        already = true;
      }
    }
    if (!already) {
      JsonObject obj = attList.add<JsonObject>();
      obj["lectureId"] = activeLectureId;
      obj["studentId"] = sid;
      obj["name"] = s["name"].as<String>();
      obj["status"] = "absent";
      obj["time"] = getCurrentDateTimeString();
    }
  }
  saveJson("/attendance.json", attendance);
  activeLectureId = "";
  server.send(200, "application/json", jsonSuccess("Attendance session ended and absences marked"));
}

void handleManualMark() {
  if (!attendanceActive) {
    server.send(400, "application/json", jsonError("No active attendance session"));
    return;
  }

  String body = server.arg("plain");
  JsonDocument doc;
  deserializeJson(doc, body);
  String studentId = doc["studentId"] | "";

  JsonDocument students = loadJson("/students.json", "[]");
  JsonArray arr = students.as<JsonArray>();
  bool found = false;
  for (JsonVariant v : arr) {
    if (v["studentId"].as<String>() == studentId) {
      found = true;
      break;
    }
  }

  if (!found) {
    server.send(400, "application/json", jsonError("Student not found"));
    return;
  }

  JsonDocument attendance = loadJson("/attendance.json", "[]");
  JsonArray a = attendance.as<JsonArray>();
  bool already = false;
  for (JsonVariant v : a) {
    if (v["lectureId"].as<String>() == activeLectureId && v["studentId"].as<String>() == studentId) {
      if (v["status"].as<String>() == "present") already = true;
      else {
        v["status"] = "present";
        v["time"] = getCurrentDateTimeString();
      }
    }
  }

  if (!already) {
    JsonObject obj = a.add<JsonObject>();
    obj["lectureId"] = activeLectureId;
    obj["studentId"] = studentId;
    obj["status"] = "present";
    obj["time"] = getCurrentDateTimeString();
    obj["name"] = findStudentNameById(studentId);
  }

  saveJson("/attendance.json", attendance);
  server.send(200, "application/json", jsonSuccess("Attendance marked"));
}

// ---------------------------
// Setup
// ---------------------------
void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connecting to WiFi");
  int retries = 0;
  while (WiFi.status() != WL_CONNECTED && retries < 60) {
    delay(500); Serial.print(".");
    retries++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(); Serial.println("WiFi connected");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println(); Serial.println("WiFi failed");
  }
}

void setupWebServer() {
  server.on("/", HTTP_GET, handleHome);
  server.on("/api/dashboard", HTTP_GET, handleDashboard);
  server.on("/api/login", HTTP_POST, handleLogin);
  server.on("/api/logout", HTTP_POST, handleLogout);
  server.on("/api/teachers", HTTP_GET, handleTeachers);
  server.on("/api/teachers", HTTP_POST, handleTeachers);
  server.on("/api/teachers", HTTP_DELETE, handleTeachers);
  server.on("/api/students", HTTP_GET, handleStudents);
  server.on("/api/students", HTTP_POST, handleStudents);
  server.on("/api/students", HTTP_DELETE, handleStudents);
  server.on("/api/subjects", HTTP_GET, handleSubjects);
  server.on("/api/subjects", HTTP_POST, handleSubjects);
  server.on("/api/subjects", HTTP_DELETE, handleSubjects);
  server.on("/api/schedules", HTTP_GET, handleSchedules);
  server.on("/api/schedules", HTTP_POST, handleSchedules);
  server.on("/api/schedules", HTTP_DELETE, handleSchedules);
  server.on("/api/teacher-dashboard", HTTP_GET, handleTeacherDashboard);
  server.on("/api/settings", HTTP_POST, handleSettings);
  server.on("/api/reports", HTTP_GET, handleReports);
  server.on("/api/fingerprint/enrollStudent", HTTP_POST, handleFingerprintEnrollStudent);
  server.on("/api/fingerprint/enrollTeacher", HTTP_POST, handleFingerprintEnrollTeacher);
  server.on("/api/attendance/start", HTTP_POST, handleAttendanceStart);
  server.on("/api/attendance/status", HTTP_GET, handleAttendanceStatus);
  server.on("/api/attendance/end", HTTP_POST, handleAttendanceEnd);
  server.on("/api/attendance/manual", HTTP_POST, handleManualMark);
  server.begin();
}

void setupPins() {
  pinMode(GREEN_LED, OUTPUT);
  pinMode(RED_LED, OUTPUT);
  pinMode(BUZZER, OUTPUT);
  digitalWrite(GREEN_LED, LOW);
  digitalWrite(RED_LED, LOW);
  digitalWrite(BUZZER, LOW);
}

void setupLCD() {
  Wire.begin(21, 22);
  lcd.init();
  lcd.backlight();
  lcd.clear();
  showLCD("ESP32 Attendance", "Booting...", "Initializing", "Please wait");
}

void setupFingerprint() {
  mySerial.begin(57600, SERIAL_8N1, 16, 17);
  finger.begin(57600);
  if (finger.verifyPassword()) {
    Serial.println("Fingerprint sensor found");
  } else {
    Serial.println("Fingerprint sensor not found");
  }
}

void setupRTC() {
  if (!rtc.begin()) {
    Serial.println("RTC not found");
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  setupPins();
  setupLCD();
  setupRTC();
  initStorage();
  createDefaultAdminIfNeeded();
  connectWiFi();
  setupFingerprint();
  setupWebServer();

  showNormalLCD();
  Serial.println("System ready");
  beep(2, 120);
}

void loop() {
  server.handleClient();
  static unsigned long lastStatusRefresh = 0;

  if (millis() - lastStatusRefresh > 1000) {
    showNormalLCD();
    lastStatusRefresh = millis();
  }

  if (attendanceActive) {
    int fingerId = scanFingerprint();
    if (fingerId != -1) {
      String studentJson = getStudentFromFingerprint(fingerId);
      JsonDocument student;
      deserializeJson(student, studentJson);

      if (student.isNull() || student["studentId"].as<String>().length() == 0) {
        Serial.println("Unknown fingerprint");
        setLed(false, true);
        beep(1, 220);
        showLCD("Unknown Finger", "Not Found", "Scan again", getCurrentTimeString());
        delay(1000);
        setLed(false, false);
      } else {
        String studentId = student["studentId"].as<String>();
        if (isStudentAlreadyPresent(studentId, activeLectureId)) {
          Serial.println("Already present");
          setLed(false, true);
          beep(2, 180);
          showLCD(student["name"].as<String>(), "Already Present", "Try next", getCurrentTimeString());
          delay(800);
          setLed(false, false);
        } else {
          JsonDocument attendance = loadJson("/attendance.json", "[]");
          JsonArray arr = attendance.as<JsonArray>();
          JsonObject obj = arr.add<JsonObject>();
          obj["lectureId"] = activeLectureId;
          obj["studentId"] = studentId;
          obj["name"] = student["name"].as<String>();
          obj["status"] = "present";
          obj["time"] = getCurrentDateTimeString();
          saveJson("/attendance.json", attendance);

          setLed(true, false);
          beep(2, 90);
          showLCD(student["name"].as<String>(), student["rollNumber"].as<String>(), "Present", getCurrentTimeString());
          Serial.println("Present");
          delay(1200);
          setLed(false, false);
        }
      }
    }
  }
}

#include "unoq-link.h"

#include <NimBLEDevice.h>

namespace UnoQLink {
namespace {

uint32_t g_linkKey = 0;
bool     g_started = false;

NimBLECharacteristic *g_snapshotChar = nullptr;
NimBLEAdvertising    *g_adv = nullptr;

volatile bool     g_connected = false;
volatile uint16_t g_mtu = 0;
uint32_t g_rx = 0, g_rejected = 0;

/// Lệnh chờ loop() rút. MỘT ô là đủ: UNO Q gửi mỗi 30 giây còn loop() rút mỗi
/// vòng, và nếu có dồn thì lệnh MỚI NHẤT mới là lệnh đúng — giữ lại lệnh cũ để
/// thi hành sau là bắn ra máy lạnh một quyết định đã lỗi thời.
volatile bool g_hasIncoming = false;
Incoming      g_incoming{};

/// `seq` của gói đã xử lý gần nhất. BLE không phát lại gói như MQTT QoS1, nhưng
/// UNO Q có thể ghi lại cùng một lệnh sau khi kết nối lại (nó không biết lệnh
/// trước đã tới hay chưa). Phát lại một khung IR là bấm remote hai lần, mà với
/// các nút xoay vòng thì lần hai nhảy sang nấc khác — tức là lặp KHÔNG vô hại.
/// Cùng lý do đã ghi ở chống-trùng req_id trong main.cpp.
uint16_t g_lastSeq = 0;
bool     g_haveSeq = false;

class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer *server, ble_gap_conn_desc *desc) override {
    g_connected = true;
    g_mtu = server->getPeerMTU(desc->conn_handle);
    Serial.printf("[unoq] da ket noi · MTU=%u\n", (unsigned)g_mtu);
    if (g_mtu && g_mtu < AC_UNOQ_MIN_MTU) {
      // Kêu to vì hỏng CÂM: notify vượt MTU bị cắt cụt, không lỗi ở cả hai bên,
      // chỉ là mấy góc cuối biến mất khỏi màn UNO Q.
      Serial.printf("[unoq] !!! MTU %u < %u byte can thiet — anh chup se bi CAT CUT.\n"
                    "       Client phai xin MTU lon hon (bleak/BlueZ thuong tu lam).\n",
                    (unsigned)g_mtu, (unsigned)AC_UNOQ_MIN_MTU);
    }
  }

  void onDisconnect(NimBLEServer *server) override {
    g_connected = false;
    g_mtu = 0;
    Serial.println("[unoq] mat ket noi — quang ba lai");
    // Quảng bá lại NGAY. NimBLE không tự làm, và không có dòng này thì UNO Q
    // khởi động lại một lần là mất đường về vĩnh viễn cho tới khi gateway reset.
    if (g_adv) g_adv->start();
  }

  void onMTUChange(uint16_t mtu, ble_gap_conn_desc *) override {
    g_mtu = mtu;
    Serial.printf("[unoq] MTU thoa thuan lai: %u\n", (unsigned)mtu);
  }
};

class CommandCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic *chr) override {
    const std::string value = chr->getValue();
    if (value.size() < sizeof(AcUnoQCommandHeader)) { g_rejected++; return; }

    AcUnoQCommandHeader hdr;
    memcpy(&hdr, value.data(), sizeof(hdr));

    if (hdr.magic != AC_UNOQ_MAGIC || hdr.version != AC_UNOQ_VERSION) {
      g_rejected++;
      return;
    }
    if (hdr.link_key != g_linkKey) {
      // Gần như chắc chắn là UNO Q của hộ khác (chung cư, hai gateway trong tầm
      // sóng). In ra vì đó là chẩn đoán, không phải tấn công — nhưng THƯA, kẻo
      // một thiết bị ghi liên tục sẽ chôn hết log.
      static uint32_t lastWarnMs = 0;
      if (millis() - lastWarnMs > 30000) {
        lastWarnMs = millis();
        Serial.println("[unoq] tu choi goi sai link_key — UNO Q cua ho khac?");
      }
      g_rejected++;
      return;
    }
    if (g_haveSeq && hdr.seq == g_lastSeq) { g_rejected++; return; }

    g_lastSeq  = hdr.seq;
    g_haveSeq  = true;
    g_rx++;

    // Chỉ ĐẶT HÀNG. Không bắn IR, không publish — xem luật 1 ở unoq-link.h.
    g_incoming.isCommand = (hdr.kind == AC_UNOQ_KIND_COMMAND);
    g_incoming.mode      = hdr.mode;
    g_incoming.setpoint  = hdr.setpoint;
    g_incoming.seq       = hdr.seq;
    g_hasIncoming = true;
  }
};

ServerCallbacks  g_serverCallbacks;
CommandCallbacks g_commandCallbacks;

}  // namespace

bool begin(const char *orgId, const char *deviceName) {
  g_linkKey = acUnoQLinkKey(orgId);

  NimBLEDevice::init(deviceName);
  // Xin MTU lớn NGAY từ đầu. Mặc định 23 byte thì ảnh chụp 44 byte bị cắt cụt
  // trong im lặng; 247 là mức mọi ngăn xếp BlueZ hiện đại chấp nhận.
  NimBLEDevice::setMTU(247);
  // +9dBm: UNO Q có thể nằm cách gateway cả phòng, sau một bức tường.
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);

  NimBLEServer *server = NimBLEDevice::createServer();
  server->setCallbacks(&g_serverCallbacks);

  NimBLEService *service = server->createService(AC_UNOQ_SERVICE_UUID);

  g_snapshotChar = service->createCharacteristic(
      AC_UNOQ_SNAPSHOT_UUID,
      // READ ngoài NOTIFY, có chủ đích: một client vừa kết nối đọc được trạng
      // thái hiện tại ngay thay vì phải đợi tới nhịp notify kế tiếp, và READ có
      // thể đi nhiều lượt ATT nên nó vẫn đủ dữ liệu ngay cả khi MTU hụt.
      NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);

  NimBLECharacteristic *cmdChar = service->createCharacteristic(
      AC_UNOQ_COMMAND_UUID,
      // WRITE có phản hồi (không phải WRITE_NR): lệnh quyết định máy nén chạy
      // hay không thì bên gửi phải biết nó đã tới nơi.
      NIMBLE_PROPERTY::WRITE);
  cmdChar->setCallbacks(&g_commandCallbacks);

  service->start();

  g_adv = NimBLEDevice::getAdvertising();
  g_adv->addServiceUUID(AC_UNOQ_SERVICE_UUID);
  // Quảng bá kèm tên để người lắp tìm được bằng bất kỳ app BLE nào lúc truy lỗi.
  g_adv->setScanResponse(true);
  if (!g_adv->start()) {
    Serial.println("[unoq] khong quang ba duoc");
    return false;
  }

  g_started = true;
  Serial.printf("[unoq] GATT san sang · ten=\"%s\" · link_key=%08X\n",
                deviceName, (unsigned)g_linkKey);
  return true;
}

void publish(const AcUnoQSnapshot &snapshot) {
  if (!g_started || g_snapshotChar == nullptr) return;
  // Ghi giá trị dù chưa ai kết nối: client vừa kết nối sẽ READ được ngay số mới
  // nhất thay vì đọc ra một khối rỗng.
  g_snapshotChar->setValue((const uint8_t *)&snapshot, sizeof(snapshot));
  if (g_connected) g_snapshotChar->notify();
}

bool poll(Incoming &out) {
  if (!g_hasIncoming) return false;
  out = g_incoming;
  g_hasIncoming = false;
  return true;
}

bool     connected() { return g_connected; }
uint16_t negotiatedMtu() { return g_mtu; }
uint32_t rxCount() { return g_rx; }
uint32_t rejectedCount() { return g_rejected; }

}  // namespace UnoQLink

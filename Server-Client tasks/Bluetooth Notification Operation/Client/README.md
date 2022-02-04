/***************************************************************************//**
 * This example demonstrates a general purpose format of client or central application for Notification operation.
 *
 ******************************************************************************/
#include "em_common.h"
#include "app_assert.h"
#include "sl_bluetooth.h"
#include "gatt_db.h"
#include "app.h"
#include "sl_sleeptimer.h"
#include "sl_bt_api.h"
#include "app_log.h"

#define CONNECTION_TIMEOUT 1

static uint8_t CONNECT_TIMEOUT_SEC = 10;
sl_sleeptimer_timer_handle_t connection_timeout_timer;

void sleep_timer_callback(sl_sleeptimer_timer_handle_t *handle, void *data)
{
  (void)data;
  (void)handle;
  sl_bt_external_signal(CONNECTION_TIMEOUT);
}

// global variable declared for the connection handle.
uint8_t conn_handle;

// global variables declared for service discovery.
uint8_t service_discovery = 0;
uint32_t service_handles[1] = {0xFFFFFFFF};
// Value Type Demo service UUID: 6270a9e9-0a20-4ad2-ac50-a263a6789c13
const uint8_t service_to_find[16] = {0x13,
                                     0x9c,
                                     0x78,
                                     0xa6,
                                     0x63,
                                     0xa2,
                                     0x50,
                                     0xac,
                                     0xd2,
                                     0x4a,
                                     0x20,
                                     0x0a,
                                     0xe9,
                                     0xa9,
                                     0x70,
                                     0x62};

//global variables declared for characteristic discovery.
uint8_t characteristic_discovery = 0;
uint16_t characteristic_handles[1] = {0xFFFF};
// User characteristic UUID: 94b3edb3-d6f2-4c39-83d3-0e38120d15c2
const uint8_t char_to_find[16] = {0xc2,
                                  0x15,
                                  0x0d,
                                  0x12,
                                  0x38,
                                  0x0e,
                                  0xd3,
                                  0x83,
                                  0x39,
                                  0x4c,
                                  0xf2,
                                  0xd6,
                                  0xb3,
                                  0xed,
                                  0xb3,
                                  0x94};

// receive buffer declared.
uint16_t notification_data[500];
#define PACKET_SIZE 50

// necessary variables for device name discovery in advertisement packets
const uint8_t device_name[19] = { 0x4e, 0x6f, 0x74, 0x69, 0x66, 0x69, 0x63, 0x61, 0x74, 0x69, 0x6f, 0x6e, 0x20, 0x53, 0x65, 0x72, 0x76, 0x65, 0x72, };
//const uint8_t target_address[6] = {0x29, 0x7a, 0xc9, 0x23, 0xa4, 0x60};
static uint8_t find_device_name_in_advertisement(uint8_t *data, uint8_t len);
//static uint8_t find_service_in_advertisement(uint8_t *data, uint8_t len);

uint8_t num_pack_received = 0;
uint16_t num_bytes_received = 0;
uint8_t i, j;

/**************************************************************************//**
 * Application Init.
 *****************************************************************************/
SL_WEAK void app_init(void)
{
  /////////////////////////////////////////////////////////////////////////////
  // Put your additional application init code here!                         //
  // This is called once during start-up.                                    //
  /////////////////////////////////////////////////////////////////////////////

}

/**************************************************************************//**
 * Application Process Action.
 *****************************************************************************/
SL_WEAK void app_process_action(void)
{
  /////////////////////////////////////////////////////////////////////////////
  // Put your additional application code here!                              //
  // This is called infinitely.                                              //
  // Do not call blocking functions from here!                               //
  /////////////////////////////////////////////////////////////////////////////
}

/**************************************************************************//**
 * Bluetooth stack event handler.
 * This overrides the dummy weak implementation.
 *
 * @param[in] evt Event coming from the Bluetooth stack.
 *****************************************************************************/
void sl_bt_on_event(sl_bt_msg_t *evt)
{
  sl_status_t sc;
  bd_addr address;
  uint8_t address_type;
  uint8_t system_id[8];

  switch (SL_BT_MSG_ID(evt->header)) {
    // -------------------------------
    // This event indicates the device has started and the radio is ready.
    // Do not call any stack command before receiving this boot event!
    case sl_bt_evt_system_boot_id:

      // Extract unique ID from BT Address.
      sc = sl_bt_system_get_identity_address(&address, &address_type);
      app_assert(sc == SL_STATUS_OK,
                 "[E: 0x%04x] Failed to get Bluetooth address\n",
                 (int)sc);

      // Pad and reverse unique ID to get System ID.
      system_id[0] = address.addr[5];
      system_id[1] = address.addr[4];
      system_id[2] = address.addr[3];
      system_id[3] = 0xFF;
      system_id[4] = 0xFE;
      system_id[5] = address.addr[2];
      system_id[6] = address.addr[1];
      system_id[7] = address.addr[0];

      sc = sl_bt_gatt_server_write_attribute_value(gattdb_system_id,
                                                   0,
                                                   sizeof(system_id),
                                                   system_id);
      app_assert(sc == SL_STATUS_OK,
                 "[E: 0x%04x] Failed to write attribute\n",
                 (int)sc);

      app_log("System Boot\r\n");

      // set scan mode.
      sc = sl_bt_scanner_set_mode(1, 0); // 0 means passive scanning mode.
      app_assert(sc == SL_STATUS_OK,
                 "[E: 0x%04x] Failed to set scanner mode \n",
                 (int)sc);

      // set scan timing.
      sc = sl_bt_scanner_set_timing(1, 160, 160);
      app_assert(sc == SL_STATUS_OK,
                    "[E: 0x%04x] Failed to set scanner timming \n",
                    (int)sc);

      // start scanning
      sl_bt_scanner_start(1, scanner_discover_observation);

      // "start the soft timer 1 to notice if it cannot find any advertiser." - Use sleeptimer or simple timer API instead as
      // sl_bt_system_soft_timer function is deprecated from v3.2
      //sl_bt_system_set_soft_timer(327680, 1, 1);
      sc = sl_sleeptimer_start_timer(&connection_timeout_timer, CONNECT_TIMEOUT_SEC * 32768, sleep_timer_callback, NULL, 0, 0);
      app_assert_status(sc);

      break;
      // -------------------------------
      // This event indicates that a new connection was opened.
      case sl_bt_evt_connection_opened_id:
        sl_bt_scanner_stop();
        app_log("connected\r\n");
        conn_handle = evt->data.evt_connection_opened.connection;
        break;

      // -------------------------------
      // This event indicates that a connection was closed.
      case sl_bt_evt_connection_closed_id:
        sl_bt_scanner_start(1, scanner_discover_observation);
        break;

      // Discovery of & connection to server device on the basis of parsing particular server device name in advertisement packets.

      case sl_bt_evt_scanner_scan_report_id:
      // Parse advertisement packets
        if (evt->data.evt_scanner_scan_report.packet_type == 0){
            // If a advertisement is found...
            if (find_device_name_in_advertisement(&(evt->data.evt_scanner_scan_report.data.data[0]),
                                                    evt->data.evt_scanner_scan_report.data.len) != 0) {
                // then stop scanning for a while
                sc = sl_bt_scanner_stop();
                app_assert(sc == SL_STATUS_OK,
                           "[E: 0x%04x] Failed to stop discovery\n",
                           (int)sc);
                // and connect to that device
                sc = sl_bt_connection_open(evt->data.evt_scanner_scan_report.address,
                                           evt->data.evt_scanner_scan_report.address_type,
                                           gap_1m_phy,
                                           &conn_handle);
                app_assert(sc == SL_STATUS_OK,
                           "[E: 0x%04x] Failed to connect\n",
                           (int)sc);
            }
        }
        break;

      case sl_bt_evt_connection_parameters_id:
        app_log("connection established\r\n");
        sl_bt_gatt_discover_primary_services(conn_handle);
        service_discovery = 1;
        break;

      case sl_bt_evt_gatt_service_id:
        if (memcmp(evt->data.evt_gatt_service.uuid.data, service_to_find, 16) == 0){
            service_handles[0] = evt->data.evt_gatt_service.service;
        }
        break;

      case sl_bt_evt_gatt_procedure_completed_id:
        if (service_discovery){
            service_discovery = 0;
            //characteristic discovery can be started here.
            sl_bt_gatt_discover_characteristics(conn_handle, service_handles[0]);
            characteristic_discovery = 1;
            break;
        }
        if (characteristic_discovery){
            characteristic_discovery = 0;
            //reading/writing remote database can be started here.
            //sl_bt_gatt_read_characteristic_value(conn_handle, characteristic_handles[0]);
            sl_bt_gatt_set_characteristic_notification(conn_handle, characteristic_handles[0], gatt_notification);
            app_log("notification enabled\r\n");
            //reading_characteristic = 1;
            break;
        }
        break;

      case sl_bt_evt_gatt_characteristic_id:
        if(memcmp(evt->data.evt_gatt_characteristic.uuid.data, char_to_find, 16) == 0){
            characteristic_handles[0] = evt->data.evt_gatt_characteristic.characteristic;
            app_log("char found!\r\n");
        }
        break;

      case sl_bt_evt_gatt_mtu_exchanged_id:
        /* Calculate maximum data per one notification / write-without-response, this depends on the MTU.
         * up to ATT_MTU-3 bytes can be sent at once  */
         //_max_packet_size = evt->data.evt_gatt_mtu_exchanged.mtu - 3;
         //_min_packet_size = _max_packet_size; /* Try to send maximum length packets whenever possible */
         app_log("MTU exchanged: %d\r\n", evt->data.evt_gatt_mtu_exchanged.mtu);
         break;

      case sl_bt_evt_gatt_characteristic_value_id:
        for(i = 0; i < evt->data.evt_gatt_characteristic_value.value.len; i++)
        {
            app_log("%d\t", evt->data.evt_gatt_characteristic_value.value.data[i]);
        }
        app_log("\r\n");

        num_pack_received++;
        app_log("notification packets received: %d\r\n", num_pack_received);
        num_bytes_received += evt->data.evt_gatt_characteristic_value.value.len;
        app_log("number of bytes received: %d\r\n", num_bytes_received);
        break;

      case sl_bt_evt_connection_phy_status_id:
        app_log("using PHY %d\r\n", evt->data.evt_connection_phy_status.phy);
        break;
    ///////////////////////////////////////////////////////////////////////////
    // Add additional event handlers here as your application requires!      //
    ///////////////////////////////////////////////////////////////////////////

    // -------------------------------
    // Default event handler.
    default:
      break;
  }
}

// Parse advertisements looking for particular server device name in advertisement.
static uint8_t find_device_name_in_advertisement(uint8_t *data, uint8_t len)
{
  uint8_t ad_field_type;
  uint8_t i = 0;
  // Parse advertisement packet
  while (i < len) {
    ad_field_type = data[i];
    // 0x09 is the ad_field_type before the device name
    if (ad_field_type == 0x09) {
      // compare device name to advertised device name
      if (memcmp(&data[i + 1], device_name, 19) == 0) {
        return 1;
      }
    }
    // advance to the next AD struct
    i++;
  }
  return 0;
}

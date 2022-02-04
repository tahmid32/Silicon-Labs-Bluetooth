/***************************************************************************//**
 * @file
 * @brief Core application logic.
 *******************************************************************************
 * # License
 * <b>Copyright 2020 Silicon Laboratories Inc. www.silabs.com</b>
 *******************************************************************************
 *
 * The licensor of this software is Silicon Laboratories Inc. Your use of this
 * software is governed by the terms of Silicon Labs Master Software License
 * Agreement (MSLA) available at
 * www.silabs.com/about-us/legal/master-software-license-agreement. This
 * software is distributed to you in Source Code format and is governed by the
 * sections of the MSLA applicable to Source Code.
 *
 ******************************************************************************/
#include "em_common.h"
#include "app_assert.h"
#include "sl_bluetooth.h"
#include "gatt_db.h"
#include "app.h"
#include "app_log.h"
#include "sl_simple_timer.h"

//#define CONNECTION_TIMEOUT 1
#define TOTAL_CHAR 510
#define PACKET_SIZE 244
#define REST (TOTAL_CHAR - ((TOTAL_CHAR / PACKET_SIZE) * PACKET_SIZE))
uint16_t TOTAL_LENGTH = 0;

// The advertising set handle allocated from Bluetooth stack.
static uint8_t advertising_set_handle = 0xff;
static uint8_t conn_handle = 0xff;

/* Allocate buffer to store the ut_user characteristic value */
static uint8_t user_char_buf[TOTAL_CHAR];
static uint8_t packet_sent = 0;

static void notify();
uint8_t i;
uint8_t k;
static uint8_t j = 0;
static uint16_t bytes_sent = 0;

// Periodic timer handle.
static sl_simple_timer_t app_periodic_timer;

// Periodic timer callback.
static void app_periodic_timer_cb(sl_simple_timer_t *timer, void *data);

/**************************************************************************//**
 * Application Init.
 *****************************************************************************/
SL_WEAK void app_init(void)
{
    // Fill test data buffer
    for (i = 0; i < (TOTAL_CHAR/2); i++) {
      user_char_buf[i] = i;
    }

    for (i = (TOTAL_CHAR/2); i > 0; i--) {
          user_char_buf[i + (j*2)] = i;
          j++;
    }

    TOTAL_LENGTH = sizeof(user_char_buf) / sizeof(user_char_buf[0]);
    app_log("Total data size: %d bytes\r\n", TOTAL_LENGTH);
    app_log("Data collection:\n");

    for (uint16_t m = 0; m<TOTAL_CHAR; m++)
    {
        app_log("%d\t", user_char_buf[m]);
    }
    app_log("\r\n");
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
      // Print boot message.
      app_log("Bluetooth stack booted: v%d.%d.%d-b%d\r\n",
                 evt->data.evt_system_boot.major,
                 evt->data.evt_system_boot.minor,
                 evt->data.evt_system_boot.patch,
                 evt->data.evt_system_boot.build);

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

      app_log("Bluetooth %s address: %02X:%02X:%02X:%02X:%02X:%02X\r\n",
               address_type ? "static random" : "public device",
               address.addr[5],
               address.addr[4],
               address.addr[3],
               address.addr[2],
               address.addr[1],
               address.addr[0]);

      sc = sl_bt_gatt_server_write_attribute_value(gattdb_system_id,
                                                   0,
                                                   sizeof(system_id),
                                                   system_id);
      app_assert(sc == SL_STATUS_OK,
                    "[E: 0x%04x] Failed to write attribute\n",
                    (int)sc);

      app_log("boot event - starting advertising\r\n");

      // Create an advertising set.
      sc = sl_bt_advertiser_create_set(&advertising_set_handle);
      app_assert(sc == SL_STATUS_OK,
                    "[E: 0x%04x] Failed to create advertising set\n",
                    (int)sc);

      // Set advertising interval to 100ms.
      sc = sl_bt_advertiser_set_timing(
        advertising_set_handle,
        160, // min. adv. interval (milliseconds * 1.6)
        160, // max. adv. interval (milliseconds * 1.6)
        0,   // adv. duration
        0);  // max. num. adv. events
      app_assert(sc == SL_STATUS_OK,
                    "[E: 0x%04x] Failed to set advertising timing\n",
                    (int)sc);
      // Start general advertising and enable connections.
      sc = sl_bt_advertiser_start(
        advertising_set_handle,
        advertiser_general_discoverable,
        advertiser_connectable_scannable);
      app_assert(sc == SL_STATUS_OK,
                    "[E: 0x%04x] Failed to start advertising\n",
                    (int)sc);
      break;

    // -------------------------------
    // This event indicates that a new connection was opened.
    case sl_bt_evt_connection_opened_id:
      app_log("connection opened\r\n");
      conn_handle = evt->data.evt_connection_opened.connection;
      break;

    // -------------------------------
    // This event indicates that a connection was closed.
    case sl_bt_evt_connection_closed_id:
      app_log("connection closed, reason: 0x%2.2x\r\n", evt->data.evt_connection_closed.reason);
      conn_handle = 0xff;
      // Restart advertising after client has disconnected.
      sc = sl_bt_advertiser_start(
        advertising_set_handle,
        advertiser_general_discoverable,
        advertiser_connectable_scannable);
      app_assert(sc == SL_STATUS_OK,
                    "[E: 0x%04x] Failed to start advertising\n",
                    (int)sc);
      break;

      /* TAG: when the remote device subscribes for notification,
       * start a timer to send out notifications periodically */
      case sl_bt_evt_gatt_server_characteristic_status_id:
        if(evt->data.evt_gatt_server_characteristic_status.characteristic == gattdb_vt_user){
            if (evt->data.evt_gatt_server_characteristic_status.status_flags
                != gatt_server_client_config) {
                break;
            }

            // use the gattdb handle as the timer handle here
            /*
            sc = sl_bt_system_set_soft_timer(evt->data.evt_gatt_server_characteristic_status.client_config_flags ? 32768 * 1 : 0,
                                             evt->data.evt_gatt_server_characteristic_status.characteristic,
                                             0);
            app_assert(sc == SL_STATUS_OK,
                      "[E: 0x%04x] Failed to start/stop software timer\n",
                      (int)sc);
            */
            sc = sl_simple_timer_start(&app_periodic_timer,
                                       evt->data.evt_gatt_server_characteristic_status.client_config_flags ? 1000 : 0,
                                       app_periodic_timer_cb,
                                       NULL,
                                       true);
            app_assert_status(sc);

        }
        break;
      /*
      case sl_bt_evt_system_soft_timer_id:
        if (bytes_sent < TOTAL_CHAR)
        {
            notify(evt->data.evt_system_soft_timer.handle);
        }
        break;
      */
    ///////////////////////////////////////////////////////////////////////////
    // Add additional event handlers here as your application requires!      //
    ///////////////////////////////////////////////////////////////////////////

    // -------------------------------
    // Default event handler.
    default:
      break;
   }
}

static void app_periodic_timer_cb(sl_simple_timer_t *timer, void *data)
{
  (void)data;
  (void)timer;
  if(bytes_sent < TOTAL_LENGTH)
  {
      notify();
  }
  //notify();
}

static void notify()
{
  sl_status_t sc;
  uint8_t length = 0;

  if((packet_sent != (TOTAL_LENGTH / PACKET_SIZE)) && (TOTAL_LENGTH > PACKET_SIZE))
  {
      uint8_t data[PACKET_SIZE];

      for (k=0; k<PACKET_SIZE; k++)
      {
          data[k] = user_char_buf[k + (packet_sent * PACKET_SIZE)];
      }

      length = sizeof(data)/sizeof(data[0]);
      app_log("Packet size to be sent now: %d\r\n", length);

      sc = sl_bt_gatt_server_send_notification(conn_handle,
                                               gattdb_vt_user,
                                               length,
                                               data);
      app_assert(sc == SL_STATUS_OK,
                 "[E: 0x%04x] Failed to send a notification\n",
                 (int)sc);
  }

  else
  {
      uint8_t data[TOTAL_LENGTH - bytes_sent];

      for (k=0; k<(TOTAL_LENGTH - bytes_sent); k++)
      {
          data[k] = user_char_buf[k + bytes_sent];
      }

      length = sizeof(data)/sizeof(data[0]);
      app_log("Packet size to be sent now: %d\r\n", length);

      sc = sl_bt_gatt_server_send_notification(conn_handle,
                                               gattdb_vt_user,
                                               length,
                                               data);
      app_assert(sc == SL_STATUS_OK,
                 "[E: 0x%04x] Failed to send a notification\n",
                 (int)sc);
  }

  bytes_sent += length;
  app_log("Sent bytes = %d\r\n", bytes_sent);
  packet_sent++;
}

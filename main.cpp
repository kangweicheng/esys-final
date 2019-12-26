#include "mbed.h"
#include "wifi.h"

#include "platform/Callback.h"
#include "events/EventQueue.h"
#include "platform/NonCopyable.h"

#include "ble/BLE.h"
#include "ble/Gap.h"
#include "ble/GattClient.h"
#include "ble/GapAdvertisingParams.h"
#include "ble/GapAdvertisingData.h"
#include "ble/GattServer.h"

// #include "pretty_printer.h"
/*------------------------------------------------------------------------------
Hyperterminal settings: 115200 bauds, 8-bit data, no parity

This example 
  - connects to a wifi network (SSID & PWD to set in mbed_app.json)
  - Connects to a TCP server (set the address in RemoteIP)
  - Sends "Hello" to the server when data is received

This example uses SPI3 ( PE_0 PC_10 PC_12 PC_11), wifi_wakeup pin (PB_13), 
wifi_dataready pin (PE_1), wifi reset pin (PE_8)
------------------------------------------------------------------------------*/

/* Private defines -----------------------------------------------------------*/
#define WIFI_WRITE_TIMEOUT 100
#define WIFI_READ_TIMEOUT  100
#define CONNECTION_TRIAL_MAX          10

/* Private typedef------------------------------------------------------------*/
/* Private macro -------------------------------------------------------------*/
/* Private variables ---------------------------------------------------------*/
Serial pc(SERIAL_TX, SERIAL_RX);
uint8_t RemoteIP[] = {MBED_CONF_APP_SERVER_IP_1,MBED_CONF_APP_SERVER_IP_2,MBED_CONF_APP_SERVER_IP_3, MBED_CONF_APP_SERVER_IP_4};

char* modulename;
uint8_t TxData[] = "STM32 : Hello!\n";
uint16_t RxLen;
uint8_t  MAC_Addr[6]; 
uint8_t  IP_Addr[4]; 
// ble section
using mbed::callback;

class BLEProcess : private mbed::NonCopyable<BLEProcess> {
public:
    /**
     * Construct a BLEProcess from an event queue and a ble interface.
     *
     * Call start() to initiate ble processing.
     */
    BLEProcess(events::EventQueue &event_queue, BLE &ble_interface,  int32_t Socket) :
        _event_queue(event_queue),
        _ble_interface(ble_interface),
        _socket(Socket),
        _post_init_cb() {
        }
        

    ~BLEProcess()
    {
        stop();
    }

   /**
     * Subscription to the ble interface initialization event.
     *
     * @param[in] cb The callback object that will be called when the ble
     * interface is initialized.
     */
    void on_init(mbed::Callback<void(BLE&, events::EventQueue&)> cb)
    {
        _post_init_cb = cb;
    }

    /**
     * Initialize the ble interface, configure it and start advertising.
     */
    bool start()
    {
        printf("Ble process started.\r\n");

        if (_ble_interface.hasInitialized()) {
            printf("Error: the ble instance has already been initialized.\r\n");
            return false;
        }

        _ble_interface.onEventsToProcess(
            makeFunctionPointer(this, &BLEProcess::schedule_ble_events)
        );

        ble_error_t error = _ble_interface.init(
            this, &BLEProcess::when_init_complete
        );

        if (error) {
            printf("Error: %u returned by BLE::init.\r\n", error);
            return false;
        }

        return true;
    }

    /**
     * Close existing connections and stop the process.
     */
    void stop()
    {
        if (_ble_interface.hasInitialized()) {
            _ble_interface.shutdown();
            printf("Ble process stopped.");
        }
    }

private:

    /**
     * Schedule processing of events from the BLE middleware in the event queue.
     */
    void schedule_ble_events(BLE::OnEventsToProcessCallbackContext *event)
    {
        _event_queue.call(mbed::callback(&event->ble, &BLE::processEvents));
    }

    /**
     * Sets up adverting payload and start advertising.
     *
     * This function is invoked when the ble interface is initialized.
     */
    void when_init_complete(BLE::InitializationCompleteCallbackContext *event)
    {
        if (event->error) {
            printf("Error %u during the initialization\r\n", event->error);
            return;
        }
        printf("Ble instance initialized\r\n");

        Gap &gap = _ble_interface.gap();
        gap.onConnection(this, &BLEProcess::when_connection);
        gap.onDisconnection(this, &BLEProcess::when_disconnection);

        if (!set_advertising_parameters()) {
            return;
        }

        if (!set_advertising_data()) {
            return;
        }

        if (!start_advertising()) {
            return;
        }

        if (_post_init_cb) {
            _post_init_cb(_ble_interface, _event_queue);
        }
    }

    void when_connection(const Gap::ConnectionCallbackParams_t *connection_event)
    {
        
        printf("Connected.\r\n");
        BLE &ble = _ble_interface;
        uint8_t address[6];
        uint8_t TxData[] = "connect";

        uint16_t Datalen;
        BLEProtocol::AddressType_t typeP;
        ble.gap().getAddress(&typeP, address);
        printf("%d:%d:%d:%d:%d:%d\n", address[5], address[4], address[3], address[2], address[1], address[0]);
        // tr_info("when_connection(); address: %s, type: %d", tr_array(address, 6), typeP);
        if(WIFI_SendData(_socket, TxData, sizeof(TxData), &Datalen, WIFI_WRITE_TIMEOUT) != WIFI_STATUS_OK) {
            printf("> ERROR : Failed to send Data.\n");   
        } 
    }

    void when_disconnection(const Gap::DisconnectionCallbackParams_t *event)
    {
        
        printf("Disconnected.\r\n");
        start_advertising();
    }

    bool start_advertising(void)
    {
        Gap &gap = _ble_interface.gap();

        /* Start advertising the set */
        ble_error_t error = gap.startAdvertising(ble::LEGACY_ADVERTISING_HANDLE);

        if (error) {
            printf("Error %u during gap.startAdvertising.\r\n", error);
            return false;
        } else {
            printf("Advertising started.\r\n");
            return true;
        }
    }

    bool set_advertising_parameters()
    {
        Gap &gap = _ble_interface.gap();

        ble_error_t error = gap.setAdvertisingParameters(
            ble::LEGACY_ADVERTISING_HANDLE,
            ble::AdvertisingParameters()
        );

        if (error) {
            printf("Gap::setAdvertisingParameters() failed with error %d", error);
            return false;
        }

        return true;
    }

    bool set_advertising_data()
    {
        Gap &gap = _ble_interface.gap();

        /* Use the simple builder to construct the payload; it fails at runtime
         * if there is not enough space left in the buffer */
        ble_error_t error = gap.setAdvertisingPayload(
            ble::LEGACY_ADVERTISING_HANDLE,
            ble::AdvertisingDataSimpleBuilder<ble::LEGACY_ADVERTISING_MAX_SIZE>()
                .setFlags()
                .setName("Final Project 2019")
                .getAdvertisingData()
        );

        if (error) {
            printf("Gap::setAdvertisingPayload() failed with error %d", error);
            return false;
        }

        return true;
    }

    events::EventQueue &_event_queue;
    BLE &_ble_interface;
    mbed::Callback<void(BLE&, events::EventQueue&)> _post_init_cb;
    int32_t _socket;
};

class ClockService {
    typedef ClockService Self;

public:
    ClockService() :
        _hour_char("485f4145-52b9-4644-af1f-7a6b9322490f", 0),
        _minute_char("0a924ca7-87cd-4699-a3bd-abdcd9cf126a", 0),
        _second_char("8dd6a1b7-bc75-4741-8a26-264af75807de", 0),
        _smart_home(
            /* uuid */ "51311102-030e-485f-b122-f8f381aa84ed",
            /* characteristics */ _clock_characteristics,
            /* numCharacteristics */ sizeof(_clock_characteristics) /
                                     sizeof(_clock_characteristics[0])
        ),
        _server(NULL),
        _event_queue(NULL)
    {
        // update internal pointers (value, descriptors and characteristics array)
        _clock_characteristics[0] = &_hour_char;
        _clock_characteristics[1] = &_minute_char;
        _clock_characteristics[2] = &_second_char;

        // setup authorization handlers
        _hour_char.setWriteAuthorizationCallback(this, &Self::authorize_client_write);
        _minute_char.setWriteAuthorizationCallback(this, &Self::authorize_client_write);
        _second_char.setWriteAuthorizationCallback(this, &Self::authorize_client_write);
    }



    void start(BLE &ble_interface, events::EventQueue &event_queue)
    {
         if (_event_queue) {
            return;
        }
        printf("coooooonnect");
        _server = &ble_interface.gattServer();
        _event_queue = &event_queue;

        // register the service
        printf("Adding demo service\r\n");
        ble_error_t err = _server->addService(_smart_home);

        if (err) {
            printf("Error %u during demo service registration.\r\n", err);
            return;
        }

        // read write handler
        _server->onDataSent(as_cb(&Self::when_data_sent));
        _server->onDataWritten(as_cb(&Self::when_data_written));
        _server->onDataRead(as_cb(&Self::when_data_read));

        // updates subscribtion handlers
        _server->onUpdatesEnabled(as_cb(&Self::when_update_enabled));
        _server->onUpdatesDisabled(as_cb(&Self::when_update_disabled));
        _server->onConfirmationReceived(as_cb(&Self::when_confirmation_received));

        // print the handles
        printf("clock service registered\r\n");
        printf("service handle: %u\r\n", _smart_home.getHandle());
        printf("\thour characteristic value handle %u\r\n", _hour_char.getValueHandle());
        printf("\tminute characteristic value handle %u\r\n", _minute_char.getValueHandle());
        printf("\tsecond characteristic value handle %u\r\n", _second_char.getValueHandle());

        _event_queue->call_every(1000 /* ms */, callback(this, &Self::increment_second));
    }

private:

    /**
     * Handler called when a notification or an indication has been sent.
     */
    void when_data_sent(unsigned count)
    {
        printf("sent %u updates\r\n", count);
    }

    /**
     * Handler called after an attribute has been written.
     */
    void when_data_written(const GattWriteCallbackParams *e)
    {
        printf("data written:\r\n");
        printf("\tconnection handle: %u\r\n", e->connHandle);
        printf("\tattribute handle: %u", e->handle);
        if (e->handle == _hour_char.getValueHandle()) {
            printf(" (hour characteristic)\r\n");
        } else if (e->handle == _minute_char.getValueHandle()) {
            printf(" (minute characteristic)\r\n");
        } else if (e->handle == _second_char.getValueHandle()) {
            printf(" (second characteristic)\r\n");
        } else {
            printf("\r\n");
        }
        printf("\twrite operation: %u\r\n", e->writeOp);
        printf("\toffset: %u\r\n", e->offset);
        printf("\tlength: %u\r\n", e->len);
        printf("\t data: ");

        for (size_t i = 0; i < e->len; ++i) {
            printf("%02X", e->data[i]);
        }

        printf("\r\n");
    }

    /**
     * Handler called after an attribute has been read.
     */
    void when_data_read(const GattReadCallbackParams *e)
    {
        printf("data read:\r\n");
        printf("\tconnection handle: %u\r\n", e->connHandle);
        printf("\tattribute handle: %u", e->handle);
        if (e->handle == _hour_char.getValueHandle()) {
            printf(" (hour characteristic)\r\n");
        } else if (e->handle == _minute_char.getValueHandle()) {
            printf(" (minute characteristic)\r\n");
        } else if (e->handle == _second_char.getValueHandle()) {
            printf(" (second characteristic)\r\n");
        } else {
            printf("\r\n");
        }
    }

    /**
     * Handler called after a client has subscribed to notification or indication.
     *
     * @param handle Handle of the characteristic value affected by the change.
     */
    void when_update_enabled(GattAttribute::Handle_t handle)
    {
        printf("update enabled on handle %d\r\n", handle);
    }

    /**
     * Handler called after a client has cancelled his subscription from
     * notification or indication.
     *
     * @param handle Handle of the characteristic value affected by the change.
     */
    void when_update_disabled(GattAttribute::Handle_t handle)
    {
        printf("update disabled on handle %d\r\n", handle);
    }

    /**
     * Handler called when an indication confirmation has been received.
     *
     * @param handle Handle of the characteristic value that has emitted the
     * indication.
     */
    void when_confirmation_received(GattAttribute::Handle_t handle)
    {
        printf("confirmation received on handle %d\r\n", handle);
    }

    /**
     * Handler called when a write request is received.
     *
     * This handler verify that the value submitted by the client is valid before
     * authorizing the operation.
     */
    void authorize_client_write(GattWriteAuthCallbackParams *e)
    {
        printf("characteristic %u write authorization\r\n", e->handle);

        if (e->offset != 0) {
            printf("Error invalid offset\r\n");
            e->authorizationReply = AUTH_CALLBACK_REPLY_ATTERR_INVALID_OFFSET;
            return;
        }

        if (e->len != 1) {
            printf("Error invalid len\r\n");
            e->authorizationReply = AUTH_CALLBACK_REPLY_ATTERR_INVALID_ATT_VAL_LENGTH;
            return;
        }

        if ((e->data[0] >= 60) ||
            ((e->data[0] >= 24) && (e->handle == _hour_char.getValueHandle()))) {
            printf("Error invalid data\r\n");
            e->authorizationReply = AUTH_CALLBACK_REPLY_ATTERR_WRITE_NOT_PERMITTED;
            return;
        }

        e->authorizationReply = AUTH_CALLBACK_REPLY_SUCCESS;
    }

    /**
     * Increment the second counter.
     */
    void increment_second(void)
    {
        uint8_t second = 0;
        ble_error_t err = _second_char.get(*_server, second);
        if (err) {
            printf("read of the second value returned error %u\r\n", err);
            return;
        }

        second = (second + 1) % 60;

        err = _second_char.set(*_server, second);
        if (err) {
            printf("write of the second value returned error %u\r\n", err);
            return;
        }

        if (second == 0) {
            increment_minute();
        }
    }

    /**
     * Increment the minute counter.
     */
    void increment_minute(void)
    {
        uint8_t minute = 0;
        ble_error_t err = _minute_char.get(*_server, minute);
        if (err) {
            printf("read of the minute value returned error %u\r\n", err);
            return;
        }

        minute = (minute + 1) % 60;

        err = _minute_char.set(*_server, minute);
        if (err) {
            printf("write of the minute value returned error %u\r\n", err);
            return;
        }

        if (minute == 0) {
            increment_hour();
        }
    }

    /**
     * Increment the hour counter.
     */
    void increment_hour(void)
    {
        uint8_t hour = 0;
        ble_error_t err = _hour_char.get(*_server, hour);
        if (err) {
            printf("read of the hour value returned error %u\r\n", err);
            return;
        }

        hour = (hour + 1) % 24;

        err = _hour_char.set(*_server, hour);
        if (err) {
            printf("write of the hour value returned error %u\r\n", err);
            return;
        }
    }

private:
    /**
     * Helper that construct an event handler from a member function of this
     * instance.
     */
    template<typename Arg>
    FunctionPointerWithContext<Arg> as_cb(void (Self::*member)(Arg))
    {
        return makeFunctionPointer(this, member);
    }

    /**
     * Read, Write, Notify, Indicate  Characteristic declaration helper.
     *
     * @tparam T type of data held by the characteristic.
     */
    template<typename T>
    class ReadWriteNotifyIndicateCharacteristic : public GattCharacteristic {
    public:
        /**
         * Construct a characteristic that can be read or written and emit
         * notification or indication.
         *
         * @param[in] uuid The UUID of the characteristic.
         * @param[in] initial_value Initial value contained by the characteristic.
         */
        ReadWriteNotifyIndicateCharacteristic(const UUID & uuid, const T& initial_value) :
            GattCharacteristic(
                /* UUID */ uuid,
                /* Initial value */ &_value,
                /* Value size */ sizeof(_value),
                /* Value capacity */ sizeof(_value),
                /* Properties */ GattCharacteristic::BLE_GATT_CHAR_PROPERTIES_READ |
                                GattCharacteristic::BLE_GATT_CHAR_PROPERTIES_WRITE |
                                GattCharacteristic::BLE_GATT_CHAR_PROPERTIES_NOTIFY |
                                GattCharacteristic::BLE_GATT_CHAR_PROPERTIES_INDICATE,
                /* Descriptors */ NULL,
                /* Num descriptors */ 0,
                /* variable len */ false
            ),
            _value(initial_value) {
        }

        /**
         * Get the value of this characteristic.
         *
         * @param[in] server GattServer instance that contain the characteristic
         * value.
         * @param[in] dst Variable that will receive the characteristic value.
         *
         * @return BLE_ERROR_NONE in case of success or an appropriate error code.
         */
        ble_error_t get(GattServer &server, T& dst) const
        {
            uint16_t value_length = sizeof(dst);
            return server.read(getValueHandle(), &dst, &value_length);
        }

        /**
         * Assign a new value to this characteristic.
         *
         * @param[in] server GattServer instance that will receive the new value.
         * @param[in] value The new value to set.
         * @param[in] local_only Flag that determine if the change should be kept
         * locally or forwarded to subscribed clients.
         */
        ble_error_t set(
            GattServer &server, const uint8_t &value, bool local_only = false
        ) const {
            return server.write(getValueHandle(), &value, sizeof(value), local_only);
        }

    private:
        uint8_t _value;
    };

    ReadWriteNotifyIndicateCharacteristic<uint8_t> _hour_char;
    ReadWriteNotifyIndicateCharacteristic<uint8_t> _minute_char;
    ReadWriteNotifyIndicateCharacteristic<uint8_t> _second_char;

    // list of the characteristics of the clock service
    GattCharacteristic* _clock_characteristics[3];

    // demo service
    GattService _smart_home;

    GattServer* _server;
    events::EventQueue *_event_queue;
    
    uint16_t Datalen;
    uint8_t RxData [500];
   
};
 uint16_t Trials = CONNECTION_TRIAL_MAX;
 int32_t Socket = -1;
// main section
int main()
{
    pc.baud(115200);
    // BLE &ble = BLE::Instance();
   

    printf("\n");
    printf("************************************************************\n");
    printf("***   STM32 IoT Discovery kit for STM32L475 MCU          ***\n");
    printf("***      WIFI Module in TCP Client mode demonstration    ***\n\n");
    printf("*** TCP Client Instructions :\n");
    printf("*** 1- Make sure your Phone is connected to the same network that\n");
    printf("***    you configured using the Configuration Access Point.\n");
    printf("*** 2- Create a server by using the android application TCP Server\n");
    printf("***    with port(8002).\n");
    printf("*** 3- Get the Network Name or IP Address of your phone from the step 2.\n\n"); 
    printf("************************************************************\n");

    /*Initialize  WIFI module */
    if(WIFI_Init() ==  WIFI_STATUS_OK) {
        printf("> WIFI Module Initialized.\n");  
        if(WIFI_GetMAC_Address(MAC_Addr) == WIFI_STATUS_OK) {
            printf("> es-wifi module MAC Address : %X:%X:%X:%X:%X:%X\n",     
                   MAC_Addr[0],
                   MAC_Addr[1],
                   MAC_Addr[2],
                   MAC_Addr[3],
                   MAC_Addr[4],
                   MAC_Addr[5]);   
        } else {
            printf("> ERROR : CANNOT get MAC address\n");
        }
    
        if( WIFI_Connect(MBED_CONF_APP_WIFI_SSID, MBED_CONF_APP_WIFI_PASSWORD, WIFI_ECN_WPA2_PSK) == WIFI_STATUS_OK) {
            printf("> es-wifi module connected \n");
            if(WIFI_GetIP_Address(IP_Addr) == WIFI_STATUS_OK) {
                printf("> es-wifi module got IP Address : %d.%d.%d.%d\n",     
                       IP_Addr[0],
                       IP_Addr[1],
                       IP_Addr[2],
                       IP_Addr[3]); 
        
                printf("> Trying to connect to Server: %d.%d.%d.%d:8002 ...\n",     
                       RemoteIP[0],
                       RemoteIP[1],
                       RemoteIP[2],
                       RemoteIP[3]);
        
                while (Trials--){ 
                    if( WIFI_OpenClientConnection(0, WIFI_TCP_PROTOCOL, "TCP_CLIENT", RemoteIP, 8002, 0) == WIFI_STATUS_OK){
                        printf("> TCP Connection opened successfully.\n"); 
                        Socket = 0;
                    }
                }
                if(!Trials) {
                    printf("> ERROR : Cannot open Connection\n");
                }
            } else {
                printf("> ERROR : es-wifi module CANNOT get IP address\n");
            }
        } else {
            printf("> ERROR : es-wifi module NOT connected\n");
        }
    } else {
        printf("> ERROR : WIFI Module cannot be initialized.\n"); 
    }

    printf("start ble init");
    BLE &ble_interface = BLE::Instance();
    events::EventQueue event_queue;
    ClockService demo_service;
    BLEProcess ble_process(event_queue, ble_interface, Socket);

    ble_process.on_init(callback(&demo_service, &ClockService::start));

    // bind the event queue to the ble interface, initialize the interface
    // and start advertising
    ble_process.start();
    event_queue.dispatch_forever();
    // Process the event queue.
    

    
    
    
    // while(1){
    //     if(Socket != -1) {
    //         if(WIFI_ReceiveData(Socket, RxData, sizeof(RxData), &Datalen, WIFI_READ_TIMEOUT) == WIFI_STATUS_OK){
    //             if(Datalen > 0) {
    //                 if(WIFI_SendData(Socket, TxData, sizeof(TxData), &Datalen, WIFI_WRITE_TIMEOUT) != WIFI_STATUS_OK) {
    //                     printf("> ERROR : Failed to send Data.\n");   
    //                 } 
    //             }
    //         } else {
    //             printf("> ERROR : Failed to Receive Data.\n");  
    //         }
    //     }
    // }
    
}

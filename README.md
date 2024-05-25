# lora-cgm-sender

### Requirements

* Arduino IDE 2.0
* Your wifi router's SSID and password
* A LibreLinkUp account connected to your Libre Freestyle account

To set up your LibreLinkUp account, do the following...

1. On the FreeStyle Libre 3 app on your phone, click the menu icon ("hamburger") at the top of the display, then click "Connected Apps". Now click on the Manage button for LibreLinkUp. Then click the Add Connection button. Enter the name and email address for a new account that you will use to call the API with. This can be any email that you choose. Please note that this account is different than the account that you put into the FreeStyle 3 Libre app when you first configured it, but you can reuse the same email address if you want. It's just that the passwords may be different
2. Upon receiving the signup email, download the LibreLinkUp app on your phone. You only need this app just one time to create a new account with the email that you chose above. Create your new account
3. Once the account is created, you should automatically be connected to your FreeStyle Libre 3 sensor

### Building the code

Open the `lora-cgm-sender.ino` file in the Arduino IDE. Next, create a `credentials.h` file in the same directory with the following contents...

```
#define WIFI_SSID "wifi ssid"  // The name of your wifi router
#define WIFI_PASSWORD "wifi password"  // The wifi router password
#define CGM_USERNAME "LibreLinkUp username"  // The username for the CGM API login
#define CGM_PASSWORD "LibreLinkUp password"  // The password for the CGM API login
```

Now, simply build and upload the file to your device

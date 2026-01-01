#define ESP_DRD_USE_SPIFFS true

// Include Libraries
//#include ".h"

#include <WiFi.h>

#include <WiFiManager.h>

#include "wManager.h"
#include "monitor.h"
#include "drivers/displays/display.h"
#include "drivers/storage/SDCard.h"
#include "drivers/storage/nvMemory.h"
#include "drivers/storage/storage.h"
#include "mining.h"
#include "timeconst.h"

#include <ArduinoJson.h>
#include <esp_flash.h>


// Flag for saving data
bool shouldSaveConfig = false;

// Variables to hold data from custom textboxes
TSettings Settings;

// Define WiFiManager Object
WiFiManager wm;
extern monitor_data mMonitor;

nvMemory nvMem;

extern SDCard SDCrd;

String readCustomAPName() {
    Serial.println("DEBUG: Attempting to read custom AP name from flash at 0x3F0000...");
    
    // Leer directamente desde flash
    const size_t DATA_SIZE = 128;
    uint8_t buffer[DATA_SIZE];
    memset(buffer, 0, DATA_SIZE); // Clear buffer
    
    // Leer desde 0x3F0000
    esp_err_t result = esp_flash_read(NULL, buffer, 0x3F0000, DATA_SIZE);
    if (result != ESP_OK) {
        Serial.printf("DEBUG: Flash read error: %s\n", esp_err_to_name(result));
        return "";
    }
    
    Serial.println("DEBUG: Successfully read from flash");
    String data = String((char*)buffer);
    
    // Debug: show raw data read
    Serial.printf("DEBUG: Raw flash data: '%s'\n", data.c_str());
    
    if (data.startsWith("WEBFLASHER_CONFIG:")) {
        Serial.println("DEBUG: Found WEBFLASHER_CONFIG marker");
        String jsonPart = data.substring(18); // Después del marcador "WEBFLASHER_CONFIG:"
        
        Serial.printf("DEBUG: JSON part: '%s'\n", jsonPart.c_str());
        
        DynamicJsonDocument doc(256);
        DeserializationError error = deserializeJson(doc, jsonPart);
        
        if (error == DeserializationError::Ok) {
            Serial.println("DEBUG: JSON parsed successfully");
            
            if (doc.containsKey("apname")) {
                String customAP = doc["apname"].as<String>();
                customAP.trim();
                
                if (customAP.length() > 0 && customAP.length() < 32) {
                    Serial.printf("✅ Custom AP name from webflasher: %s\n", customAP.c_str());
                    return customAP;
                } else {
                    Serial.printf("DEBUG: AP name invalid length: %d\n", customAP.length());
                }
            } else {
                Serial.println("DEBUG: 'apname' key not found in JSON");
            }
        } else {
            Serial.printf("DEBUG: JSON parse error: %s\n", error.c_str());
        }
    } else {
        Serial.println("DEBUG: WEBFLASHER_CONFIG marker not found - no custom config");
    }
    
    Serial.println("DEBUG: Using default AP name");
    return "";
}

void saveConfigCallback()
// Callback notifying us of the need to save configuration
{
    Serial.println("Should save config");
    shouldSaveConfig = true;    
    //wm.setConfigPortalBlocking(false);
}

/* void saveParamsCallback()
// Callback notifying us of the need to save configuration
{
    Serial.println("Should save config");
    shouldSaveConfig = true;
    nvMem.saveConfig(&Settings);
} */

void configModeCallback(WiFiManager* myWiFiManager)
// Called when config mode launched
{
    Serial.println("Entered Configuration Mode");
    drawSetupScreen();
    Serial.print("Config SSID: ");
    Serial.println(myWiFiManager->getConfigPortalSSID());

    Serial.print("Config IP Address: ");
    Serial.println(WiFi.softAPIP());
}

void reset_configuration()
{
    Serial.println("Erasing Config, restarting");
    nvMem.deleteConfig();
    resetStat();
    wm.resetSettings();
    ESP.restart();
}

void init_WifiManager()
{
#ifdef MONITOR_SPEED
    Serial.begin(MONITOR_SPEED);
#else
    Serial.begin(115200);
#endif //MONITOR_SPEED
    //Serial.setTxTimeoutMs(10);
    
    // Check for custom AP name from flasher config, otherwise use default
    String customAPName = readCustomAPName();
    const char* apName = customAPName.length() > 0 ? customAPName.c_str() : DEFAULT_SSID;

    //Init pin 15 to eneble 5V external power (LilyGo bug)
#ifdef PIN_ENABLE5V
    pinMode(PIN_ENABLE5V, OUTPUT);
    digitalWrite(PIN_ENABLE5V, HIGH);
#endif

    // Change to true when testing to force configuration every time we run
    bool forceConfig = false; // Production mode - only show portal when needed

#if defined(PIN_BUTTON_2)
    // Check if button2 is pressed to enter configMode with actual configuration
    if (!digitalRead(PIN_BUTTON_2)) {
        Serial.println(F("Button pressed to force start config mode"));
        forceConfig = true;
        wm.setBreakAfterConfig(true); //Set to detect config edition and save
    }
#endif
    // Explicitly set WiFi mode
    WiFi.mode(WIFI_STA);

    if (!nvMem.loadConfig(&Settings))
    {
        //No config file on internal flash.
        if (SDCrd.loadConfigFile(&Settings))
        {
            //Config file on SD card.
            SDCrd.SD2nvMemory(&nvMem, &Settings); // reboot on success.          
        }
        else
        {
            //No config file on SD card. Starting wifi config server.
            forceConfig = true;
        }
    };
    
    // Free the memory from SDCard class 
    SDCrd.terminate();
    
    // Reset settings (only for development)
    //wm.resetSettings();

    //Set dark theme
    //wm.setClass("invert"); // dark theme

    // Set config save notify callback
    wm.setSaveConfigCallback(saveConfigCallback);
    wm.setSaveParamsCallback(saveConfigCallback);

    // Set callback that gets called when connecting to previous WiFi fails, and enters Access Point mode
    wm.setAPCallback(configModeCallback);    

    //Advanced settings
    wm.setConfigPortalBlocking(false); //Hacemos que el portal no bloquee el firmware
    wm.setConnectTimeout(40); // how long to try to connect for before continuing
    wm.setConfigPortalTimeout(180); // auto close configportal after n seconds
    wm.setCaptivePortalEnable(true); // Enable captive portal to redirect all traffic to config page
    wm.setAPClientCheck(true); // avoid timeout if client connected to softap

    // Set menu title and translate menu items to German
    wm.setTitle("WiFiManager");
    wm.setHostname(apName);
    
    // Add DNS handler to catch all captive portal detection attempts
    wm.setAPStaticIPConfig(IPAddress(192,168,4,1), IPAddress(192,168,4,1), IPAddress(255,255,255,0));
    
    // Use custom menu with German text (no Info button)
    std::vector<const char *> menu = {};
    wm.setMenu(menu);
    wm.setCustomMenuHTML(
        "<h2 style='text-align:center;color:#F27121;margin-top:5px;margin-bottom:30px;font-weight:500;font-size:16px;'>VexaMinerAP</h2>"
        "<form action='/wifi' method='get'><button>WLAN konfigurieren</button></form><br/>"
    );

    // Custom CSS styling with accent color #F27121 and modern multi-step design - German localized
    const char* customCSS = 
        "<style>"
        ":root{--accent:#F27121;--bg:#1a1a1a;--card:#2d2d2d;--text:#fff;--text-dim:#a0a0a0;}"
        "*{box-sizing:border-box;}"
        "body{background:var(--bg);font-family:'Segoe UI',system-ui,sans-serif;margin:0;padding:20px;color:var(--text);}"
        
        // Main wrapper and headings
        ".wrap{max-width:480px;margin:0 auto;text-align:center;}"
        "h1,h2,h3,.title{color:var(--accent);margin:0 0 20px 0;font-weight:600;text-align:center;}"
        "h1,.title{font-size:28px;text-align:center;margin-bottom:0;line-height:1.2;}"
        ".wrap>div:first-child,.wrap>h1:first-child,.wrap>*:first-child{text-align:center;}"
        
        // Menu buttons - WiFiManager uses direct <a> tags inside .wrap on main page
        ".wrap>a{display:block;background:var(--accent);color:#fff!important;border:none;padding:14px 28px;border-radius:8px;font-size:15px;font-weight:600;text-align:center;text-decoration:none;margin:10px 0;transition:all .2s;}"
        ".wrap>a:hover{background:#e56a1a;transform:translateY(-1px);box-shadow:0 4px 12px rgba(242,113,33,.3);}"
        ".wrap>a:active{transform:translateY(0);}"
        
        // WiFi network list - scrollable container
        ".wifi-list-container{max-height:280px;overflow-y:auto;overflow-x:hidden;margin:10px 0;padding-right:4px;}"
        ".wifi-list-container::-webkit-scrollbar{width:6px;}"
        ".wifi-list-container::-webkit-scrollbar-track{background:#1a1a1a;border-radius:4px;}"
        ".wifi-list-container::-webkit-scrollbar-thumb{background:#555;border-radius:4px;}"
        ".wifi-list-container::-webkit-scrollbar-thumb:hover{background:var(--accent);}"
        
        // Style WiFiManager's network divs (have onclick="c(this)")
        "div[onclick^='c(']{background:var(--card);border-radius:8px;margin:8px 0;padding:12px 16px;border:2px solid #444;transition:all .2s;cursor:pointer;color:#fff;}"
        "div[onclick^='c(']:hover{border-color:var(--accent);background:#3a3a3a;}"
        ".q{color:var(--accent);font-weight:600;margin-left:8px;}"
        ".l{color:var(--text-dim);margin-left:8px;}"
        
        // Cards and form styling
        ".card{background:var(--card);border-radius:12px;padding:24px;margin:20px 0;box-shadow:0 4px 6px rgba(0,0,0,.3);}"
        ".card h3{font-size:18px;margin-bottom:16px;display:flex;align-items:center;gap:8px;color:var(--text);}"
        ".step-number{background:var(--accent);color:#fff;width:28px;height:28px;border-radius:50%;display:inline-flex;align-items:center;justify-content:center;font-size:14px;font-weight:600;}"
        
        // Form elements
        "label{display:block;color:var(--text)!important;margin:16px 0 8px 0;font-size:14px;font-weight:500;}"
        "input[type='text'],input[type='password'],input[type='number'],select{width:100%;padding:14px;background:#0d0d0d!important;border:2px solid #555!important;border-radius:8px;color:#fff!important;font-size:15px;box-sizing:border-box;transition:all .2s;}"
        "input:focus,select:focus{outline:none!important;border-color:var(--accent)!important;background:#0d0d0d!important;box-shadow:0 0 0 3px rgba(242,113,33,.2)!important;color:#fff!important;}"
        "input::placeholder{color:#888!important;}"
        "input:-webkit-autofill,input:-webkit-autofill:hover,input:-webkit-autofill:focus{-webkit-text-fill-color:#fff!important;-webkit-box-shadow:0 0 0 1000px #0d0d0d inset!important;transition:background-color 5000s ease-in-out 0s;}"
        
        // Buttons
        "button,input[type='submit']{background:var(--accent);color:#fff;border:none;padding:14px 28px;border-radius:8px;font-size:15px;font-weight:600;cursor:pointer;width:100%;transition:all .2s;margin-top:20px;}"
        "button:hover,input[type='submit']:hover{background:#e56a1a;transform:translateY(-1px);box-shadow:0 4px 12px rgba(242,113,33,.3);}"
        "button:active,input[type='submit']:active{transform:translateY(0);}"
        ".btn-secondary{background:#444;margin-top:10px;}"
        ".btn-secondary:hover{background:#555;box-shadow:none;}"
        
        // Checkboxes
        ".checkbox-wrapper{display:flex;align-items:center;gap:10px;margin:16px 0;padding:12px;background:#1a1a1a;border-radius:8px;}"
        ".checkbox-wrapper input[type='checkbox']{width:20px;height:20px;accent-color:var(--accent);cursor:pointer;margin:0;}"
        ".checkbox-wrapper label{margin:0!important;cursor:pointer;}"
        
        // Help text and info boxes
        ".help-text{font-size:12px;color:#999;margin-top:4px;display:block;}"
        ".info-box{background:rgba(242,113,33,.1);border-left:3px solid var(--accent);padding:12px 16px;border-radius:4px;margin:16px 0;font-size:14px;color:var(--text);}"
        
        // Collapsible advanced settings
        "details{margin:20px 0;}"
        "summary{cursor:pointer;padding:14px 18px;background:#2d2d2d;border-radius:8px;color:var(--accent);font-weight:600;user-select:none;transition:all .2s;font-size:15px;}"
        "summary:hover{background:#3a3a3a;}"
        "details[open] summary{margin-bottom:16px;}"
        
        // Info page styling
        "dt{color:var(--accent);font-weight:600;margin-top:12px;}"
        "dd{color:var(--text);margin:4px 0 0 0;}"
        
        // Message box
        ".msg{background:var(--card);border-radius:8px;padding:12px;margin:20px 0;text-align:center;color:var(--text);}"
        "p{color:var(--text);margin:10px 0;}"
        "br{display:none;}"
        "form{max-width:100%;}"
        
        // German translations for menu items (handles both long and short paths)
        ".wrap>a[href='/wifi'],.wrap>a[href='/w']{font-size:0;}"
        ".wrap>a[href='/wifi']::after,.wrap>a[href='/w']::after{content:'WLAN konfigurieren';font-size:15px;}"
        ".wrap>a[href='/0wifi'],.wrap>a[href='/0w']{font-size:0;}"
        ".wrap>a[href='/0wifi']::after,.wrap>a[href='/0w']::after{content:'WLAN konfigurieren (ohne Scan)';font-size:15px;}"
        ".wrap>a[href='/info'],.wrap>a[href='/i']{font-size:0;}"
        ".wrap>a[href='/info']::after,.wrap>a[href='/i']::after{content:'Info';font-size:15px;}"
        ".wrap>a[href='/exit'],.wrap>a[href='/close'],.wrap>a[href='/x']{font-size:0;}"
        ".wrap>a[href='/exit']::after,.wrap>a[href='/close']::after,.wrap>a[href='/x']::after{content:'Beenden';font-size:15px;}"
        ".wrap>a[href='/update'],.wrap>a[href='/u']{font-size:0;}"
        ".wrap>a[href='/update']::after,.wrap>a[href='/u']::after{content:'Aktualisieren';font-size:15px;}"
        ".wrap>a[href='/erase'],.wrap>a[href='/r']{font-size:0;}"
        ".wrap>a[href='/erase']::after,.wrap>a[href='/r']::after{content:'Zurücksetzen';font-size:15px;}"
        ".wrap>a[href='/restart']{font-size:0;}"
        ".wrap>a[href='/restart']::after{content:'Neustarten';font-size:15px;}"
        
        // Responsive
        "@media(max-width:480px){body{padding:10px;}}"
        "</style>"
        
        "<script>"
        "document.addEventListener('DOMContentLoaded',function(){"
        
        // 1. Translate menu buttons by href (most reliable)
        "document.querySelectorAll('a').forEach(function(a){"
        "var h=a.getAttribute('href')||'';"
        "if(h.indexOf('wifi')>=0||h.indexOf('/w')>=0)a.textContent='WLAN konfigurieren';"
        "else if(h.indexOf('info')>=0||h==='/i')a.textContent='Info';"
        "else if(h.indexOf('exit')>=0||h.indexOf('close')>=0)a.textContent='Beenden';"
        "else if(h.indexOf('update')>=0||h==='/u')a.textContent='Aktualisieren';"
        "else if(h.indexOf('erase')>=0||h==='/r')a.textContent='Zurücksetzen';"
        "});"
        
        // 2. Translate by text content (fallback)
        "document.querySelectorAll('a').forEach(function(a){"
        "var t=(a.textContent||'').trim().toLowerCase().replace(/\\s+/g,' ');"
        "if(t.indexOf('configure')>=0)a.textContent='WLAN konfigurieren';"
        "});"
        
        // 3. Translate status messages
        "document.querySelectorAll('div,p,span').forEach(function(el){"
        "var t=(el.textContent||'').trim().toLowerCase();"
        "if(t==='no ap set'||t==='no ap'||t.indexOf('no ap')===0)el.textContent='Kein AP gesetzt';"
        "if(t==='saved')el.textContent='Gespeichert!';"
        "});"
        
        // 4. Center title and wrap
        "var wrap=document.querySelector('.wrap');"
        "if(wrap)wrap.style.textAlign='center';"
        "document.querySelectorAll('h1,h2,.title').forEach(function(h){"
        "h.style.textAlign='center';"
        "});"
        
        // 3. Check if we're on config page (has form)
        "var form=document.querySelector('form');"
        "if(!form)return;"
        
        // 4. Wrap WiFi networks in scrollable card - find by .q/.l signal spans
        "var nets=[];"
        "document.querySelectorAll('.q,.l').forEach(function(s){"
        "var d=s.parentElement;"
        "if(d&&d.tagName==='DIV'&&nets.indexOf(d)<0)nets.push(d);"
        "});"
        "if(nets.length>0){"
        "var p=nets[0].parentNode;"
        "if(p){"
        "var card=document.createElement('div');"
        "card.className='card';"
        "card.style.padding='16px';"
        "var h=document.createElement('h3');"
        "h.textContent='Verfügbare Netzwerke';"
        "h.style.cssText='margin-bottom:12px;color:#fff;text-align:left';"
        "card.appendChild(h);"
        "var scroll=document.createElement('div');"
        "scroll.className='wifi-list-container';"
        "card.appendChild(scroll);"
        "p.insertBefore(card,nets[0]);"
        "for(var i=0;i<nets.length;i++)scroll.appendChild(nets[i]);"
        "}}"
        
        // 5. Find Step 1 card
        "var step1Card=null;"
        "document.querySelectorAll('.card').forEach(function(c){"
        "if(c.innerHTML.indexOf('WLAN-Verbindung')>=0)step1Card=c;"
        "});"
        
        // 6. Get SSID/Password fields
        "var ssidLabel=document.querySelector('label[for=\"s\"]');"
        "var ssidInput=document.querySelector('input[name=\"s\"]');"
        "var pwdLabel=document.querySelector('label[for=\"p\"]');"
        "var pwdInput=document.querySelector('input[name=\"p\"]');"
        
        // 7. Find Show Password checkbox - first checkbox not in a card (it's orphaned in .wrap)
        "var showPwdCb=null;"
        "document.querySelectorAll('input[type=\"checkbox\"]').forEach(function(cb){"
        "if(!showPwdCb&&!cb.closest('.card'))showPwdCb=cb;"
        "});"
        
        // 8. Move fields into Step 1 card
        "if(step1Card){"
        "var infoBox=step1Card.querySelector('.info-box');"
        
        // Move SSID
        "if(ssidLabel&&ssidInput){"
        "ssidLabel.textContent='SSID';"
        "if(infoBox){infoBox.after(ssidLabel);ssidLabel.after(ssidInput);}"
        "ssidInput.placeholder='WLAN-Name';"
        "}"
        
        // Move Password
        "if(pwdLabel&&pwdInput){"
        "if(ssidInput){ssidInput.after(pwdLabel);pwdLabel.after(pwdInput);}"
        "pwdLabel.textContent='Passwort';"
        "pwdInput.placeholder='WLAN-Passwort';"
        "}"
        
        // Move Show Password checkbox into card
        "if(showPwdCb&&pwdInput){"
        "var oldSib=showPwdCb.nextSibling;"
        "while(oldSib&&oldSib.nodeType===3)oldSib=oldSib.nextSibling;"
        "if(oldSib)oldSib.remove();"
        "var wrap=document.createElement('div');"
        "wrap.className='checkbox-wrapper';"
        "var lbl=document.createElement('label');"
        "lbl.textContent='Passwort anzeigen';"
        "lbl.style.cursor='pointer';"
        "lbl.onclick=function(){showPwdCb.click();};"
        "wrap.appendChild(showPwdCb);"
        "wrap.appendChild(lbl);"
        "pwdInput.after(wrap);"
        "}"
        "}"
        
        // 9. Wrap remaining checkboxes
        "document.querySelectorAll('input[type=\"checkbox\"]').forEach(function(cb){"
        "if(cb.closest('.checkbox-wrapper'))return;"
        "var lbl=cb.nextElementSibling;"
        "if(lbl&&lbl.tagName==='LABEL'){"
        "var w=document.createElement('div');"
        "w.className='checkbox-wrapper';"
        "cb.parentNode.insertBefore(w,cb);"
        "w.appendChild(cb);w.appendChild(lbl);"
        "}"
        "});"
        
        // 10. Set placeholders
        "var pI=document.querySelector('input[name=\"Poolport\"]');"
        "if(pI){pI.type='number';pI.min='1';pI.max='65535';pI.placeholder='3333';}"
        "var tI=document.querySelector('input[name=\"TimeZone\"]');"
        "if(tI){tI.type='number';tI.min='-12';tI.max='14';}"
        "var bI=document.querySelector('input[name=\"Brightness\"]');"
        "if(bI){bI.type='number';bI.min='0';bI.max='255';}"
        "var btc=document.querySelector('input[name=\"btcAddress\"]');"
        "if(btc)btc.placeholder='bc1q... oder 1... oder 3...';"
        "var pu=document.querySelector('input[name=\"Poolurl\"]');"
        "if(pu)pu.placeholder='pool.example.com';"
        "var pp=document.querySelector('input[name=\"Poolpassword\"]');"
        "if(pp)pp.placeholder='(Optional)';"
        
        // 11. Style submit button
        "var btn=form.querySelector('button[type=\"submit\"],input[type=\"submit\"]');"
        "if(btn){"
        "btn.value='Speichern & Verbinden';"
        "if(btn.tagName==='BUTTON')btn.textContent='Speichern & Verbinden';"
        "form.onsubmit=function(){"
        "btn.value='⏳ Wird gespeichert...';"
        "if(btn.tagName==='BUTTON')btn.textContent='⏳ Wird gespeichert...';"
        "btn.disabled=true;"
        "};"
        "}"
        "});"
        "</script>";

    wm.setCustomHeadElement(customCSS);

    // Step 1 - WiFi Configuration (WiFiManager auto-generates SSID/password fields)
    WiFiManagerParameter wifi_header("<div class='card'><h3><span class='step-number'>1</span> WLAN-Verbindung</h3>");
    WiFiManagerParameter wifi_info("<div class='info-box'>⬆️ Wählen Sie Ihr WLAN-Netzwerk aus der obigen Liste und geben Sie unten das Passwort ein.</div>");
    // Note: WiFiManager inserts SSID & Password fields here automatically
    WiFiManagerParameter wifi_footer("</div>"); // Close Step 1 card AFTER WiFi fields

    // Step 2 - Pool Configuration
    WiFiManagerParameter step2_header("<div class='card'><h3><span class='step-number'>2</span> Pool-Konfiguration</h3>");
    WiFiManagerParameter step2_info("<div class='info-box'>Mining-Pool-Verbindungseinstellungen konfigurieren.</div>");
    
    // Pool settings
    WiFiManagerParameter pool_text_box("Poolurl", "Pool-URL", Settings.PoolAddress.c_str(), 80);
    
    char convertedValue[6];
    sprintf(convertedValue, "%d", Settings.PoolPort);
    WiFiManagerParameter port_text_box_num("Poolport", "Pool-Port", convertedValue, 7);
    
    WiFiManagerParameter password_text_box("Poolpassword", "Pool-Passwort", Settings.PoolPassword, 80);
    WiFiManagerParameter pool_pwd_help("<span class='help-text'>(Optional)</span>");
    
    WiFiManagerParameter step2_footer("</div>");

    // Step 3 - Wallet Address
    WiFiManagerParameter step3_header("<div class='card'><h3><span class='step-number'>3</span> Wallet-Adresse</h3>");
    WiFiManagerParameter step3_info("<div class='info-box'>Bitcoin-Wallet-Adresse für Mining-Belohnungen eingeben.</div>");
    
    WiFiManagerParameter addr_text_box("btcAddress", "Bitcoin-Adresse", Settings.BtcWallet, 80);
    
    WiFiManagerParameter step3_footer("</div>");

    // Advanced Settings (Collapsible)
    WiFiManagerParameter advanced_start("<details><summary>⚙️ Erweiterte Einstellungen</summary><div class='card'>");
    
    // Timezone with Germany default (+1)
    char charZone[6];
    // Use saved timezone only if config was properly loaded (PoolPort > 0 means valid config)
    int defaultTimezone = 1;
    if (Settings.PoolPort > 0 && Settings.Timezone >= -12 && Settings.Timezone <= 14) {
        defaultTimezone = Settings.Timezone;
    }
    sprintf(charZone, "%d", defaultTimezone);
    WiFiManagerParameter time_text_box_num("TimeZone", "Zeitzone (UTC)", charZone, 3);
    WiFiManagerParameter tz_help("<span class='help-text'>Deutschland: +1 UTC (Standard)</span>");

    // Save stats checkbox
    char checkboxParams[25] = "type=\"checkbox\"";
    if (Settings.saveStats) {
        strcat(checkboxParams, " checked");
    }
    WiFiManagerParameter save_stats_to_nvs("SaveStatsToNVS", "Mining-Statistiken im Flash speichern", "T", 2, checkboxParams, WFM_LABEL_AFTER);

    #if defined(ESP32_2432S028R) || defined(ESP32_2432S028_2USB)
    // Display settings
    char checkboxParams2[25] = "type=\"checkbox\"";
    if (Settings.invertColors) {
        strcat(checkboxParams2, " checked");
    }
    WiFiManagerParameter invertColors("inverColors", "Display-Farben invertieren", "T", 2, checkboxParams2, WFM_LABEL_AFTER);
    
    char brightnessConvValue[4];
    sprintf(brightnessConvValue, "%d", Settings.Brightness);
    WiFiManagerParameter brightness_text_box_num("Brightness", "Display-Helligkeit (0-255)", brightnessConvValue, 3);
    #endif
    
    WiFiManagerParameter advanced_end("</div></details>");

    // Add all parameters in organized order: WiFi info -> Pool -> Wallet -> Advanced
    wm.addParameter(&wifi_header);
    wm.addParameter(&wifi_info);
    // WiFiManager auto-inserts SSID & Password fields here
    wm.addParameter(&wifi_footer); // Close Step 1 card after WiFi fields
    
    wm.addParameter(&step2_header);
    wm.addParameter(&step2_info);
    wm.addParameter(&pool_text_box);
    wm.addParameter(&port_text_box_num);
    wm.addParameter(&password_text_box);
    wm.addParameter(&pool_pwd_help);
    wm.addParameter(&step2_footer);
    
    wm.addParameter(&step3_header);
    wm.addParameter(&step3_info);
    wm.addParameter(&addr_text_box);
    wm.addParameter(&step3_footer);
    
    wm.addParameter(&advanced_start);
    wm.addParameter(&time_text_box_num);
    wm.addParameter(&tz_help);
    wm.addParameter(&save_stats_to_nvs);
    #if defined(ESP32_2432S028R) || defined(ESP32_2432S028_2USB)
    wm.addParameter(&invertColors);
    wm.addParameter(&brightness_text_box_num);
    #endif
    wm.addParameter(&advanced_end);

    Serial.println("AllDone: ");
    if (forceConfig)    
    {
        // Run if we need a configuration
        //No configuramos timeout al modulo
        wm.setConfigPortalBlocking(true); //Hacemos que el portal SI bloquee el firmware
        drawSetupScreen();
        mMonitor.VexaStatus = NM_Connecting;
        wm.startConfigPortal(apName, DEFAULT_WIFIPW);

        if (shouldSaveConfig)
        {
            //Could be break forced after edditing, so save new config
            Serial.println("failed to connect and hit timeout");
            Settings.PoolAddress = pool_text_box.getValue();
            Settings.PoolPort = atoi(port_text_box_num.getValue());
            strncpy(Settings.PoolPassword, password_text_box.getValue(), sizeof(Settings.PoolPassword));
            strncpy(Settings.BtcWallet, addr_text_box.getValue(), sizeof(Settings.BtcWallet));
            Settings.Timezone = atoi(time_text_box_num.getValue());
            //Serial.println(save_stats_to_nvs.getValue());
            Settings.saveStats = (strncmp(save_stats_to_nvs.getValue(), "T", 1) == 0);
            #if defined(ESP32_2432S028R) || defined(ESP32_2432S028_2USB)
                Settings.invertColors = (strncmp(invertColors.getValue(), "T", 1) == 0);
            #endif
            #if defined(ESP32_2432S028R) || defined(ESP32_2432S028_2USB)
                Settings.Brightness = atoi(brightness_text_box_num.getValue());
            #endif
            nvMem.saveConfig(&Settings);
            delay(3*SECOND_MS);
            //reset and try again, or maybe put it to deep sleep
            ESP.restart();            
        };
    }
    else
    {
        //Tratamos de conectar con la configuración inicial ya almacenada
        mMonitor.VexaStatus = NM_Connecting;
        // disable captive portal redirection
        wm.setCaptivePortalEnable(true); 
        wm.setConfigPortalBlocking(true);
        wm.setEnableConfigPortal(true);
        // if (!wm.autoConnect(Settings.WifiSSID.c_str(), Settings.WifiPW.c_str()))
        if (!wm.autoConnect(apName, DEFAULT_WIFIPW))
        {
            Serial.println("Failed to connect to configured WIFI, and hit timeout");
            if (shouldSaveConfig) {
                // Save new config            
                Settings.PoolAddress = pool_text_box.getValue();
                Settings.PoolPort = atoi(port_text_box_num.getValue());
                strncpy(Settings.PoolPassword, password_text_box.getValue(), sizeof(Settings.PoolPassword));
                strncpy(Settings.BtcWallet, addr_text_box.getValue(), sizeof(Settings.BtcWallet));
                Settings.Timezone = atoi(time_text_box_num.getValue());
                // Serial.println(save_stats_to_nvs.getValue());
                Settings.saveStats = (strncmp(save_stats_to_nvs.getValue(), "T", 1) == 0);
                #if defined(ESP32_2432S028R) || defined(ESP32_2432S028_2USB)
                Settings.invertColors = (strncmp(invertColors.getValue(), "T", 1) == 0);
                #endif
                #if defined(ESP32_2432S028R) || defined(ESP32_2432S028_2USB)
                Settings.Brightness = atoi(brightness_text_box_num.getValue());
                #endif
                nvMem.saveConfig(&Settings);
                vTaskDelay(2000 / portTICK_PERIOD_MS);      
            }        
            ESP.restart();                            
        } 
    }
    
    //Conectado a la red Wifi
    if (WiFi.status() == WL_CONNECTED) {
        //tft.pushImage(0, 0, MinerWidth, MinerHeight, MinerScreen);
        Serial.println("");
        Serial.println("WiFi connected");
        Serial.print("IP address: ");
        Serial.println(WiFi.localIP());


        // Lets deal with the user config values

        // Copy the string value
        Settings.PoolAddress = pool_text_box.getValue();
        //strncpy(Settings.PoolAddress, pool_text_box.getValue(), sizeof(Settings.PoolAddress));
        Serial.print("PoolString: ");
        Serial.println(Settings.PoolAddress);

        //Convert the number value
        Settings.PoolPort = atoi(port_text_box_num.getValue());
        Serial.print("portNumber: ");
        Serial.println(Settings.PoolPort);

        // Copy the string value
        strncpy(Settings.PoolPassword, password_text_box.getValue(), sizeof(Settings.PoolPassword));
        Serial.print("poolPassword: ");
        Serial.println(Settings.PoolPassword);

        // Copy the string value
        strncpy(Settings.BtcWallet, addr_text_box.getValue(), sizeof(Settings.BtcWallet));
        Serial.print("btcString: ");
        Serial.println(Settings.BtcWallet);

        //Convert the number value
        Settings.Timezone = atoi(time_text_box_num.getValue());
        Serial.print("TimeZone fromUTC: ");
        Serial.println(Settings.Timezone);

        #if defined(ESP32_2432S028R) || defined(ESP32_2432S028_2USB)
        Settings.invertColors = (strncmp(invertColors.getValue(), "T", 1) == 0);
        Serial.print("Invert Colors: ");
        Serial.println(Settings.invertColors);        
        #endif

        #if defined(ESP32_2432S028R) || defined(ESP32_2432S028_2USB)
        Settings.Brightness = atoi(brightness_text_box_num.getValue());
        Serial.print("Brightness: ");
        Serial.println(Settings.Brightness);
        #endif

    }

    // Save the custom parameters to FS (if settings changed)
    if (shouldSaveConfig)
    {
        nvMem.saveConfig(&Settings);
        #if defined(ESP32_2432S028R) || defined(ESP32_2432S028_2USB)
         if (Settings.invertColors) ESP.restart();                
        #endif
        #if defined(ESP32_2432S028R) || defined(ESP32_2432S028_2USB)
        if (Settings.Brightness != 250) ESP.restart();
        #endif
    }
}

//----------------- MAIN PROCESS WIFI MANAGER --------------
int oldStatus = 0;

void wifiManagerProcess() {

    wm.process(); // avoid delays() in loop when non-blocking and other long running code

    int newStatus = WiFi.status();
    if (newStatus != oldStatus) {
        if (newStatus == WL_CONNECTED) {
            Serial.println("CONNECTED - Current ip: " + WiFi.localIP().toString());
        } else {
            Serial.print("[Error] - current status: ");
            Serial.println(newStatus);
        }
        oldStatus = newStatus;
    }
}

#pragma once
// UI strings: English, Catalan, Spanish (Settings > LANGUAGE). UTF-8 here; drawn through
// utf8ToCp437(), so lowercase accents show and uppercase ones the font lacks lose them.
// Limits: setting labels 16 chars, hints 39, help notes 14.
#include "app.h"

#define STRINGS(X) \
  X(S_HOME, "HOME", "INICI", "INICIO") \
  X(S_CHATS, "CHATS", "XATS", "CHATS") \
  X(S_CONTACTS, "CONTACTS", "CONTACTES", "CONTACTOS") \
  X(S_HEARD, "HEARD", "ESCOLTATS", "OÍDOS") \
  X(S_METRICS, "METRICS", "MÈTRIQUES", "MÉTRICAS") \
  X(S_GPS, "GPS", "GPS", "GPS") \
  X(S_SETTINGS, "SETTINGS", "CONFIG", "AJUSTES") \
  X(S_MAP, "MAP", "MAPA", "MAPA") \
  X(S_CHAT, "CHAT", "XAT", "CHAT") \
  X(S_NODE, "NODE", "NODE", "NODO") \
  X(S_ALL, "ALL", "TOTS", "TODOS") \
  X(S_CHANNELS, "CHANNELS", "CANALS", "CANALES") \
  X(S_DIRECT, "DIRECT", "DIRECTE", "DIRECTO") \
  X(S_T_CHAT, "CHAT", "XAT", "CHAT") \
  X(S_T_RPT, "RPT", "REP", "REP") \
  X(S_T_ROOM, "ROOM", "SALA", "SALA") \
  X(S_T_SENS, "SENS", "SENS", "SENS") \
  X(S_RADIO, "RADIO", "RÀDIO", "RADIO") \
  X(S_RXLOG, "RX LOG", "REGISTRE RX", "REGISTRO RX") \
  X(S_SETUP, "SETUP", "INICIAL", "INICIAL") \
  /* hints */ \
  X(S_H_HOME, "ENT last chat  a advert  TAB  i help", "ENT últim xat  a anunci  TAB  i ajuda", "ENT últ. chat  a anuncio  TAB  i ayuda") \
  X(S_H_CHATS, "ENT open  ,/ filter  n new#  c channels", "ENT obrir  ,/ filtre  n nou#  c canals", "ENT abrir  ,/ filtro  n #nuevo  c canal") \
  X(S_H_CONTACTS, "ENT details  ,/ filter  ;. move", "ENT detalls  ,/ filtre  ;. moure", "ENT detalles  ,/ filtro  ;. mover") \
  X(S_H_HEARD, "d discover  a/A advert  ENT contact", "d descobrir  a/A anunci  ENT contacte", "d descubrir  a/A anuncio  ENT contacto") \
  X(S_H_METRICS, ",/ view  TAB page  i help", ",/ vista  TAB pàgina  i ajuda", ",/ vista  TAB página  i ayuda") \
  X(S_H_GPS, "g GPS on/off  l use as node location", "g GPS on/off  l usar com a ubicació", "g GPS on/off  l usar como ubicación") \
  X(S_H_MAP, ";.,/ pan  -= zoom  n node  l here  c me", ";.,/ mou  -= zoom  n node  l aquí  c jo", ";.,/ ir  -= zoom  n nodo  l aquí  c yo") \
  X(S_H_SETTINGS, ";. move  ,/ change  ENT edit", ";. moure  ,/ canviar  ENT editar", ";. mover  ,/ cambiar  ENT editar") \
  X(S_H_CHAT, "ENT send  ` back  Fn;. scroll", "ENT envia  ` torna  Fn;. scroll", "ENT envía  ` atrás  Fn;. mover") \
  X(S_H_CONTACT, ";.,/ choose  ENT run  ` back", ";.,/ triar  ENT executar  ` enrere", ";.,/ elegir  ENT ejecutar  ` atrás") \
  X(S_H_CHANNELS, "n #  p private  DEL remove  ENT key", "n #  p privat  DEL esborrar  ENT clau", "n #  p privado  DEL borrar  ENT clave") \
  X(S_H_ANYKEY, "any key closes", "qualsevol tecla tanca", "cualquier tecla cierra") \
  X(S_H_INPUT, "ENT ok  ` cancel  DEL erase", "ENT d'acord  ` cancel·la  DEL esborra", "ENT aceptar  ` cancelar  DEL borra") \
  X(S_H_WIZARD, ";. choose  ENT ok  ` skip", ";. triar  ENT d'acord  ` ometre", ";. elegir  ENT aceptar  ` omitir") \
  X(S_H_WIZDONE, "ENT advert & start  ` just start", "ENT anunci i comença  ` comença", "ENT anuncio y empezar  ` empezar") \
  X(S_H_CONFIRM, "y / ENT yes   other key no", "y / ENT sí   altra tecla no", "y / ENT sí   otra tecla no") \
  /* metrics */ \
  X(S_M_RX, "RX PKTS", "PAQ. RX", "PAQ. RX") \
  X(S_M_TX, "TX PKTS", "PAQ. TX", "PAQ. TX") \
  X(S_M_FLOOD, "FLOOD TX/RX", "FLOOD TX/RX", "FLOOD TX/RX") \
  X(S_M_DIRECT, "DIR TX/RX", "DIR TX/RX", "DIR TX/RX") \
  X(S_M_ERR, "RX ERRORS", "ERRORS RX", "ERRORES RX") \
  X(S_M_AIRTX, "AIR TX", "AIRE TX", "AIRE TX") \
  X(S_M_AIRRX, "AIR RX", "AIRE RX", "AIRE RX") \
  X(S_M_RSSI, "LAST RSSI", "ÚLTIM RSSI", "ÚLT. RSSI") \
  X(S_M_SNR, "LAST SNR", "ÚLTIM SNR", "ÚLT. SNR") \
  X(S_M_NOISE, "NOISE", "SOROLL", "RUIDO") \
  X(S_M_UPTIME, "UPTIME", "EN MARXA", "ACTIVO") \
  X(S_M_BATT, "BATTERY", "BATERIA", "BATERÍA") \
  X(S_M_HEAP, "FREE RAM", "RAM LLIURE", "RAM LIBRE") \
  X(S_M_HEAPMIN, "MIN RAM", "RAM MÍN.", "RAM MÍN.") \
  X(S_M_QUEUE, "APP QUEUE", "CUA APP", "COLA APP") \
  X(S_M_DUTY, "DUTY 1H", "CICLE 1H", "CICLO 1H") \
  X(S_M_LIMIT, "EU limit 10%", "límit UE 10%", "límite UE 10%") \
  /* settings */ \
  X(S_TEMPLATE, "TEMPLATE", "PLANTILLA", "PLANTILLA") \
  X(S_FREQ, "FREQUENCY", "FREQÜÈNCIA", "FRECUENCIA") \
  X(S_BW, "BANDWIDTH", "AMPLE DE BANDA", "ANCHO DE BANDA") \
  X(S_SF, "SPREAD FACTOR", "FACTOR SF", "FACTOR SF") \
  X(S_CR, "CODING RATE", "CODIFICACIÓ", "CODIFICACIÓN") \
  X(S_TXPOWER, "TX POWER", "POTÈNCIA TX", "POTENCIA TX") \
  X(S_PATHHASH, "PATH HASH", "HASH CAMÍ", "HASH RUTA") \
  X(S_RXBOOST, "RX BOOST", "GUANY RX", "GANANCIA RX") \
  X(S_SCOPE, "REGION SCOPE", "ÀMBIT REGIÓ", "ÁMBITO REGIÓN") \
  X(S_RELAY, "RELAY (REPEAT)", "REPETIDOR", "REPETIDOR") \
  X(S_NAME, "NAME", "NOM", "NOMBRE") \
  X(S_LOCATION, "LOCATION", "UBICACIÓ", "UBICACIÓN") \
  X(S_SHARELOC, "SHARE LOCATION", "COMPARTIR UBIC.", "COMPARTIR UBIC.") \
  X(S_ADVERT, "SEND ADVERT", "ENVIAR ANUNCI", "ENVIAR ANUNCIO") \
  X(S_FLOODADV, "FLOOD ADVERT", "ANUNCI GLOBAL", "ANUNCIO GLOBAL") \
  X(S_AUTOADD, "AUTO-ADD", "AFEGIR AUTO", "AÑADIR AUTO") \
  X(S_ADD_CHAT, "ADD CHAT", "AFEGIR XAT", "AÑADIR CHAT") \
  X(S_ADD_RPT, "ADD REPEATER", "AFEGIR REPETIDOR", "AÑADIR REPETIDOR") \
  X(S_ADD_ROOM, "ADD ROOM", "AFEGIR SALA", "AÑADIR SALA") \
  X(S_ADD_SENS, "ADD SENSOR", "AFEGIR SENSOR", "AÑADIR SENSOR") \
  X(S_OVERWRITE, "OVERWRITE OLD", "SOBREESCRIURE", "SOBRESCRIBIR") \
  X(S_MAXHOPS, "MAX HOPS", "SALTS MÀX.", "SALTOS MÁX.") \
  X(S_PHONE, "PHONE APP", "APP MÒBIL", "APP MÓVIL") \
  X(S_BLUETOOTH, "BLUETOOTH", "BLUETOOTH", "BLUETOOTH") \
  X(S_PIN, "BLE PIN", "PIN BLE", "PIN BLE") \
  X(S_GPSINT, "GPS INTERVAL", "INTERVAL GPS", "INTERVALO GPS") \
  X(S_TIMEZONE, "TIME ZONE", "ZONA HORÀRIA", "ZONA HORARIA") \
  X(S_DST, "SUMMER TIME", "HORARI D'ESTIU", "HORARIO VERANO") \
  X(S_SETTIME, "SET TIME", "POSAR L'HORA", "FIJAR HORA") \
  X(S_DISPLAY, "DISPLAY", "PANTALLA", "PANTALLA") \
  X(S_THEME, "THEME", "TEMA", "TEMA") \
  X(S_BRIGHT, "BRIGHTNESS", "BRILLANTOR", "BRILLO") \
  X(S_TIMEOUT, "SCREEN OFF", "APAGAR PANT.", "APAGAR PANT.") \
  X(S_LANGUAGE, "LANGUAGE", "IDIOMA", "IDIOMA") \
  X(S_ALERTS, "ALERTS", "AVISOS", "AVISOS") \
  X(S_SOUND, "SOUND", "SO", "SONIDO") \
  X(S_VOLUME, "VOLUME", "VOLUM", "VOLUMEN") \
  X(S_LED, "LED", "LED", "LED") \
  X(S_SYSTEM, "SYSTEM", "SISTEMA", "SISTEMA") \
  X(S_SDCARD, "SD CARD", "TARGETA SD", "TARJETA SD") \
  X(S_ABOUT, "ABOUT", "QUANT A", "ACERCA DE") \
  X(S_REBOOT, "REBOOT", "REINICIAR", "REINICIAR") \
  X(S_FACTORY, "FACTORY RESET", "RESTABLIR", "RESTABLECER") \
  /* values */ \
  X(S_ON, "ON", "SÍ", "SÍ") \
  X(S_OFF, "OFF", "NO", "NO") \
  X(S_CUSTOM, "Custom", "Personalitzat", "Personalizado") \
  X(S_NONE, "none", "cap", "ninguno") \
  X(S_RELAY_NA, "needs 869.495", "cal 869.495", "requiere 869.495") \
  X(S_UNSET, "not set", "sense definir", "sin definir") \
  X(S_BYTYPE, "BY TYPE", "PER TIPUS", "POR TIPO") \
  X(S_ANY, "any", "qualsevol", "cualquiera") \
  X(S_FREE, "free", "lliure", "libre") \
  X(S_SD_NONE, "none (flash)", "cap (flash)", "no (flash)") \
  X(S_AUTO_EU, "AUTO (EU)", "AUTO (UE)", "AUTO (UE)") \
  X(S_TIME_SEC, "TIME", "HORA", "HORA") \
  X(S_TIME_BACK, "can't set the clock back", "no es pot endarrerir el rellotge", "no se puede atrasar el reloj") \
  X(S_TIME_SET, "clock set", "hora fixada", "hora fijada") \
  /* chat & lists */ \
  X(S_DELIVERED, "delivered", "lliurat", "entregado") \
  X(S_FAILED, "failed", "error", "falló") \
  X(S_SENDING, "sending", "enviant", "enviando") \
  X(S_SENT, "sent", "enviat", "enviado") \
  X(S_HOPS, "hops", "salts", "saltos") \
  X(S_NO_TIME, "time not set", "hora no definida", "hora no definida") \
  X(S_NO_RECENT, "no messages yet", "encara no hi ha missatges", "aún no hay mensajes") \
  X(S_NO_CHATS, "no chats", "cap xat", "sin chats") \
  X(S_NO_CONTACTS, "no contacts yet: send an advert (a)", "cap contacte: envia un anunci (a)", "sin contactos: envía un anuncio (a)") \
  X(S_COL_NODE, "NODE", "NODE", "NODO") \
  X(S_COL_HOPS, "HOPS", "SALTS", "SALTOS") \
  X(S_NO_HEARD, "nothing heard yet", "encara no s'ha sentit res", "aún no se ha oído nada") \
  X(S_NO_FIX, "no fix", "sense posició", "sin posición") \
  X(S_SATS, "sats", "sats", "sats") \
  X(S_NEED_POS, "no position: GPS fix or set location", "sense posició: GPS o fixa ubicació", "sin posición: GPS o fija ubicación") \
  X(S_NO_LOCATED, "no contacts share a location", "cap contacte comparteix ubicació", "ningún contacto comparte ubicación") \
  X(S_EMPTY_CHAT, "no messages: say hi", "sense missatges: digues hola", "sin mensajes: di hola") \
  X(S_HEARD_AGO, "heard", "escoltat", "oído") \
  X(S_PATH, "PATH", "CAMÍ", "RUTA") \
  X(S_FLOOD, "FLOOD", "FLOOD", "FLOOD") \
  X(S_DISCOVERING, "discovering...", "descobrint...", "descubriendo...") \
  /* contact actions */ \
  X(S_A_MESSAGE, "MESSAGE", "MISSATGE", "MENSAJE") \
  X(S_A_LOGIN, "LOGIN", "ENTRAR", "ENTRAR") \
  X(S_A_STATUS, "STATUS", "ESTAT", "ESTADO") \
  X(S_A_TELEMETRY, "TELEMETRY", "TELEMETRIA", "TELEMETRÍA") \
  X(S_A_TRACE, "TRACE", "TRAÇA", "TRAZA") \
  X(S_A_RESET, "RESET PATH", "REINICIAR CAMÍ", "REINICIAR RUTA") \
  X(S_A_SHARE, "SHARE", "COMPARTIR", "COMPARTIR") \
  X(S_A_FAV, "FAVOURITE", "PREFERIT", "FAVORITO") \
  X(S_A_UNFAV, "UNFAVOURITE", "TREURE PREFERIT", "QUITAR FAVORITO") \
  X(S_A_DELETE, "DELETE", "ESBORRAR", "BORRAR") \
  /* help */ \
  X(S_K_PAGE, "next page", "pàgina seg.", "pág. sig.") \
  X(S_K_JUMP, "go to page", "anar a pàg.", "ir a pág.") \
  X(S_K_UPDOWN, "up/down", "amunt/avall", "arriba/abajo") \
  X(S_K_LEFTRIGHT, "tab/adjust", "pestanya/ajust", "pestaña/ajuste") \
  X(S_K_OPEN, "open/ok", "obrir/ok", "abrir/ok") \
  X(S_K_BACK, "back", "enrere", "atrás") \
  X(S_K_ADVERT, "advert/flood", "anunci/global", "anuncio/global") \
  X(S_K_DISCOVER, "discover", "descobrir", "descubrir") \
  X(S_K_NEWHASH, "new #channel", "nou #canal", "nuevo #canal") \
  X(S_K_CHANNELS, "channels", "canals", "canales") \
  X(S_K_GPS, "GPS on/off", "GPS on/off", "GPS on/off") \
  X(S_K_SETLOC, "set location", "fixar ubicació", "fijar ubic.") \
  X(S_K_BLACKOUT, "screen off", "apagar pant.", "apagar pant.") \
  X(S_K_HELP, "help", "ajuda", "ayuda") \
  X(S_K_SCROLL, "scroll chat", "desplaçar xat", "desplazar chat") \
  X(S_K_DEL, "erase/remove", "esborrar", "borrar") \
  X(S_K_ZOOM, "map zoom", "zoom del mapa", "zoom del mapa") \
  /* inputs & toasts */ \
  X(S_IN_HASHTAG, "Hashtag channel name", "Nom del canal hashtag", "Nombre del canal hashtag") \
  X(S_IN_CHNAME, "Private channel name", "Nom del canal privat", "Nombre del canal privado") \
  X(S_IN_KEY, "Key: 32 hex digits", "Clau: 32 dígits hex", "Clave: 32 dígitos hex") \
  X(S_IN_PASSWORD, "Password (blank = guest)", "Contrasenya (buit = convidat)", "Contraseña (vacío = invitado)") \
  X(S_INVALID, "invalid", "no vàlid", "no válido") \
  X(S_ADDED, "added", "afegit", "añadido") \
  X(S_DELETED, "deleted", "esborrat", "borrado") \
  X(S_DONE, "done", "fet", "hecho") \
  X(S_SEND_FAILED, "send failed", "error d'enviament", "error al enviar") \
  X(S_ADVERT_SENT, "advert sent", "anunci enviat", "anuncio enviado") \
  X(S_ADVERT_FLOOD, "flood advert sent", "anunci global enviat", "anuncio global enviado") \
  X(S_NOT_CONTACT, "not a contact (yet)", "encara no és contacte", "aún no es contacto") \
  X(S_LOC_SET, "location set", "ubicació fixada", "ubicación fijada") \
  X(S_CONFIRM, "ARE YOU SURE?", "N'ESTÀS SEGUR?", "¿SEGURO?") \
  X(S_WAITING, "waiting for reply...", "esperant resposta...", "esperando respuesta...") \
  X(S_KEY_HEX, "key (hex):", "clau (hex):", "clave (hex):") \
  X(S_STARTING, "starting radio...", "engegant ràdio...", "iniciando radio...") \
  X(S_LOGGED_IN, "logged in", "sessió iniciada", "sesión iniciada") \
  X(S_LOGIN_FAILED, "login failed (password?)", "accés denegat (contrasenya?)", "acceso denegado (¿contraseña?)") \
  X(S_ADMIN, "admin", "admin", "admin") \
  X(S_YES, "yes", "sí", "sí") \
  X(S_NO, "no", "no", "no") \
  X(S_NO_DATA, "no data", "sense dades", "sin datos") \
  X(S_NEED_PATH, "needs a known path", "cal un camí conegut", "requiere ruta conocida") \
  X(S_BACK_HERE, "back here", "de tornada", "de vuelta") \
  X(S_NO_RESPONSE, "no response", "sense resposta", "sin respuesta") \
  X(S_NEW_CONTACT, "new contact", "nou contacte", "nuevo contacto") \
  X(S_APPLIED, "applied", "aplicat", "aplicado") \
  X(S_REBOOT_APPLY, "reboot to apply", "reinicia per aplicar", "reinicia para aplicar") \
  X(S_PUBKEY, "public key:", "clau pública:", "clave pública:") \
  /* setup wizard */ \
  X(S_W_WELCOME, "Welcome to CardCore!", "Benvingut a CardCore!", "¡Bienvenido a CardCore!") \
  X(S_W_NAME, "Node name: others will see it", "Nom del node: el veuran tots", "Nombre del nodo: lo verán todos") \
  X(S_W_LOC_GPS, "use GPS fix", "usar posició GPS", "usar posición GPS") \
  X(S_W_LOC_TYPE, "type lat,lon", "escriure lat,lon", "escribir lat,lon") \
  X(S_W_SKIP, "skip", "ometre", "omitir") \
  X(S_W_LOC_INPUT, "lat,lon (e.g. 41.3874,2.1686)", "lat,lon (ex. 41.3874,2.1686)", "lat,lon (ej. 41.3874,2.1686)") \
  X(S_W_WAIT_FIX, "GPS on: wait for a fix, or type it", "GPS engegat: espera posició o escriu-la", "GPS activo: espera posición o escríbela") \
  X(S_W_BLE_ON, "ON: the phone app can connect", "SÍ: l'app mòbil s'hi pot connectar", "SÍ: la app móvil puede conectar") \
  X(S_W_BLE_OFF, "OFF: saves battery", "NO: estalvia bateria", "NO: ahorra batería") \
  X(S_W_READY, "READY", "A PUNT", "LISTO") \
  X(S_W_ADVERT, "a flood advert lets others find you", "l'anunci global fa que et trobin", "con un anuncio global te encontrarán")

#define STR_ID(id, en, ca, es) id,
enum StrId : uint16_t { S_NULL, STRINGS(STR_ID) NSTR };
#undef STR_ID

static inline const char* tr(int id) {
#define STR_ROW(id, en, ca, es) {en, ca, es},
  static const char* const T[][3] = {{"", "", ""}, STRINGS(STR_ROW)};
#undef STR_ROW
  return id > 0 && id < NSTR ? T[id][app_ui().lang % 3] : "";
}

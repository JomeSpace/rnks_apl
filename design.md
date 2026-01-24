# Designentscheidungen

## Architektur und Schichten
- Strikte Trennung von Anwendung und Protokoll:
  - Anwendung: `src/client.c` und `src/server.c` (Datei lesen/schreiben, CLI)
  - Protokoll + UDP: `src/clientSy.c` und `src/serverSy.c`
- Pro Instanz genau ein Socket, keine Threads

## Transport und Adressierung
- UDPv6 (IPv6) als Transport, wie gefordert.
- Client verwendet `connect()` auf dem UDP-Socket, damit `send()`/`recv()` ohne Zieladresse genutzt werden können.
- Server nutzt `recvfrom()` und merkt sich die Client-Adresse für `sendto()`.

## Zeitsynchrones Sendeverhalten
- Pro Intervall maximal ein Paketversand (oder Retransmit) gemäß der Aufgabenstellung.
- `select()` wartet auf ACK oder Intervallende. Kommt ein ACK früh, wird bis Intervallende gewartet.
- Intervalllänge: `GBN_TIMEOUT_INT_MS` (in `src/data.h`).

## Fensterverwaltung und Ringpuffer (Client)
- GBN-Fenster über `base` (kleinste unbestaetigte SeqNr) und `seq_num` (nächste SeqNr).
- Ringpuffer `buffer[GBN_BUFFER_SIZE]` für gesendete noch nicht bestätigte Pakete.
- Retransmit hat Vorrang vor neuen Paketen.

## Timer und Retransmit
- Timeout als Vielfaches von Intervallen (`GBN_TIMEOUT_UNITS`).
- `timeout_counter` erhöht sich nur, wenn es ausstehende Pakete gibt und `base` sich nicht bewegt.
- Bei Timeout: Go-Back-N ab `base`, schrittweise Retransmits pro Intervall.

## ACK-Logik (Client)
- Kumulative ACKs: `AnswOk.SeNo` ist die naechste erwartete SeqNr
- ACKs auserhalb des Fensters werden verworfen (`ack_no < base` oder `ack_no > seq_num`).
- Bei Fortschritt: `base = ack_no`, `timeout_counter = 0`.

## Empfaengerlogik (Server)
- Kein Empfänger-Puffer: Out-of-Order Pakete werden verworfen.
- Trotzdem wird immer ein kumulativer ACK mit `expected_seq` gesendet.
- `expected_seq` wird nur bei in-order Datenpaketen erhoeht

## Verbindungsaufbau und -abbau
- Verbindungsaufbau per `ReqHello`/`AnswHello`.
- Verbindungsabbau per `ReqClose`/`AnswOk` nach dem Drain (alle Pakete bestätigt).

## Verlustsimulation
- Request-Verlust (`lossReq`) in `processRequest()`.
- ACK-Verlust (`lossAck`) in der Server-Hauptschleife vor `sendAnswer()`.

## Diagramme (theorie/)
- Weg-Zeit-Diagramme für fehlerfreien Fall, Paketverlust, ACK-Verlust, GBN W=5,
  Intervall-Timing, ACK ausserhalb Fenster und Fenstervergleich.
- Zustandsdiagramme für Client und Server.
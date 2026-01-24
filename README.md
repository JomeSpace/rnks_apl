# RnKs_APL

#Aufruf von Client und server

Der Client wird über die Kommandozeile mit den folgenden Parametern aufgerufen: ./client -a <server> -p <port> -f <file> -w <window>. Dabei steht -a <server> für die Adresse des Servers (z. B. localhost oder eine IP-Adresse; als Default wird loopback verwendet), -p <port> für den Port, auf dem der Server lauscht ( DEFAULT_PORT = 3333), -f <file> für den Dateinamen der zu sendenden Eingabedatei und -w <window> für die Größe des Sendefensters im Bereich 1 bis 10.
Der Server wird mit dem Befehl ./server -p <port> -f <outfile> [-r <lossReq>] [-a <lossAck>] gestartet. Hier bedeutet -p <port> den Port, auf dem der Server Anfragen annimmt (DEFAULT_PORT = 3333), -f <outfile> den Namen der Ausgabedatei, in die der Server die empfangenen Daten schreibt, -r <lossReq> optional die Wahrscheinlichkeit, mit der ein Request-/Datenpaket verloren geht (im Intervall 0.0 bis 1.0) und -a <lossAck> optional die Wahrscheinlichkeit, mit der ein ACK verloren geht (ebenfalls 0.0 bis 1.0).

Die Textdateien zur Ein und Ausgabe sollten standardmäßig in /src abgelegt werden.
Um die Ausführung zu testen, wird zunächst der Server gestartet (z. B. auf einem bestimmten Port), der dann im Hintergrund läuft und auf Client-Verbindungen wartet. Danach wird der Client in einem separaten Terminal gestartet, wobei er die gewünschte Serveradresse, den Port und die zu sendende Datei (/src) sowie die Fenstergröße angibt. Der Client sendet die Datei an den Server, der diese in die Ausgabedatei schreibt; beide Prozesse müssen sich im selben Netzwerk (oder lokal) finden können.


#Hinweis zum einrichten von Client und Server
Falls sie das MakeFile zum compailieren von server und client nutzen wollen müssen sie davor noch einen /build Ordner erstellen. Das MakeFile legt dort standartmäßig beide ausführbare Programme ab. Ansonsten lassen sich die Skripte auch standardmäßig, falls installiert direkt über gcc compailieren.

# Hinweis input.txt
Inhalt ist ein Beispiel Text von GutenbergProjekt.de. Wurde für das Testing verwendet.

#Erklärung auf die selbstständige Anfertigung

Die Client- und Server-Programme wurden selbstständig in C implementiert. Die Grundlagen der Go-Back-N-Logik wurden anhand der Vorlesungsunterlagen erarbeitet. Die Implementierung des Protokolls (z. B. Fenster- und Timeout-Logik, Fehlermodelle mittels lossReq und lossAck, sowie die Dateiübertragung) erfolgte größtenteils vollständig eigenständig und ohne direkte Übernahme von Code. Zur Netzwerkprogrammierung (z. B. Socket-Anlegen, connect, bind, send, recv, select) sowie der Timout-Logik wurden KI-gestützte Hilfstools verwendet (bspw. Chat-GPT, Claude Code). Dadurch konnte die Schnittstelle zwischen Client und Server korrekt implementiert werden. Die KI-Ausgaben wurden geprüft und entsprechend der Aufgabenstellung angepasst, sodass diese im Einklang mit dem eigens verfassten Bestandscode stehen.

Die Lösung wurde unter Linux getestet und ist im Labor U515 kompilier- und lauffähig. Die Programme werden mit den vorgeschriebenen Parametern aufgerufen, geben bei falscher Aufrufweise die geforderten Usage-Hinweise aus und übertragen die angegebene Datei im vorgegebenen Format.
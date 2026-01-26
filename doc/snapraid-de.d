Name{number}
	snapraid - SnapRAID Sicherung für Festplatten-Arrays

Synopsis
	:snapraid [-c, --conf CONFIG]
	:	[-f, --filter PATTERN] [-d, --filter-disk NAME]
	:	[-m, --filter-missing] [-e, --filter-error]
	:	[-a, --audit-only] [-h, --pre-hash] [-i, --import DIR]
	:	[-p, --plan PERC|bad|new|full]
	:	[-o, --older-than DAYS] [-l, --log FILE]
	:	[-s, --spin-down-on-error] [-w, --bw-limit RATE]
	:	[-Z, --force-zero] [-E, --force-empty]
	:	[-U, --force-uuid] [-D, --force-device]
	:	[-N, --force-nocopy] [-F, --force-full]
	:	[-R, --force-realloc]
	:	[-S, --start BLKSTART] [-B, --count BLKCOUNT]
	:	[-L, --error-limit NUMBER]
	:	[-A, --stats]
	:	[-v, --verbose] [-q, --quiet]
	:	status|smart|probe|up|down|diff|sync|scrub|fix|check
	:	|list|dup|pool|devices|touch|rehash

	:snapraid [-V, --version] [-H, --help] [-C, --gen-conf CONTENT]

Description
	SnapRAID ist ein Sicherungsprogramm, das für Festplatten-Arrays
	entwickelt wurde und Paritätsinformationen zur Datenwiederherstellung
	bei Ausfall von bis zu sechs Festplatten speichert.

	SnapRAID ist hauptsächlich für Home-Media-Center mit großen,
	selten geänderten Dateien konzipiert und bietet folgende Funktionen:

	* Sie können bereits mit Dateien gefüllte Festplatten verwenden,
		ohne sie neu formatieren zu müssen, und wie gewohnt darauf zugreifen.
	* Alle Ihre Daten werden gehasht, um die Datenintegrität zu
		gewährleisten und stille Beschädigung (silent corruption) zu verhindern.
	* Wenn die Anzahl der ausgefallenen Festplatten die Paritätsanzahl
		überschreitet, beschränkt sich der Datenverlust auf die betroffenen
		Festplatten; Daten auf anderen Festplatten bleiben zugänglich.
	* Wenn Sie versehentlich Dateien auf einer Festplatte löschen,
		ist eine Wiederherstellung möglich.
	* Festplatten können unterschiedliche Größen haben.
	* Sie können jederzeit Festplatten hinzufügen.
	* SnapRAID sperrt Ihre Daten nicht ein; Sie können die Verwendung
		jederzeit beenden, ohne neu formatieren oder Daten verschieben zu müssen.
	* Um auf eine Datei zuzugreifen, muss nur eine einzige Festplatte
		drehen (spinnen), was Strom spart und Geräusche reduziert.

	Für weitere Informationen besuchen Sie bitte die offizielle SnapRAID-Website:

		:https://www.snapraid.it/

Limitations
	SnapRAID ist ein Hybrid aus einem RAID- und einem Sicherungsprogramm,
	das darauf abzielt, die besten Vorteile beider zu kombinieren.
	Es hat jedoch einige Einschränkungen, die Sie vor der Verwendung
	berücksichtigen sollten.

	Die Haupteinschränkung besteht darin, dass wenn eine Festplatte
	ausfällt und Sie in letzter Zeit keine Synchronisierung durchgeführt haben,
	Sie möglicherweise nicht vollständig wiederherstellen können.
	Genauer gesagt können Sie möglicherweise nicht bis zur Größe der
	geänderten oder gelöschten Dateien seit der letzten `sync`-Operation
	wiederherstellen. Dies tritt auch dann auf, wenn die geänderten oder
	gelöschten Dateien sich nicht auf der ausgefallenen Festplatte befinden.
	Aus diesem Grund ist SnapRAID besser für Daten geeignet, die sich
	selten ändern.

	Andererseits verhindern neu hinzugefügte Dateien die Wiederherstellung
	von bereits vorhandenen Dateien nicht. Sie verlieren nur die
	kürzlich hinzugefügten Dateien, wenn diese sich auf der ausgefallenen
	Festplatte befinden.

	Weitere Einschränkungen von SnapRAID sind:

	* Mit SnapRAID haben Sie immer noch separate Dateisysteme für jede
		Festplatte. Mit RAID erhalten Sie ein einziges großes Dateisystem.
	* SnapRAID streift (stripes) keine Daten.
		Mit RAID erhalten Sie einen Geschwindigkeitsvorteil durch Striping.
	* SnapRAID unterstützt keine Echtzeit-Wiederherstellung.
		Mit RAID müssen Sie die Arbeit nicht unterbrechen, wenn eine
		Festplatte ausfällt.
	* SnapRAID kann Daten nur von einer begrenzten Anzahl von Festplattenausfällen
		wiederherstellen. Mit einer Sicherung können Sie von einem
		vollständigen Ausfall des gesamten Festplatten-Arrays wiederherstellen.
	* Nur Dateinamen, Zeitstempel, symbolische Links (symlinks) und
		Hardlinks werden gespeichert. Berechtigungen, Eigentümerschaft
		und erweiterte Attribute werden nicht gespeichert.

Getting Started
	Um SnapRAID zu verwenden, müssen Sie zuerst eine Festplatte in Ihrem
	Festplatten-Array auswählen, die für `parity`-Informationen
	reserviert wird. Mit einer Festplatte für Parität können Sie von
	einem einzelnen Festplattenausfall wiederherstellen, ähnlich wie bei RAID5.

	Wenn Sie von mehr Festplattenausfällen wiederherstellen möchten,
	ähnlich wie bei RAID6, müssen Sie zusätzliche Festplatten für Parität
	reservieren. Jede zusätzliche Paritätsfestplatte ermöglicht die
	Wiederherstellung von einem weiteren Festplattenausfall.

	Als Paritätsfestplatten müssen Sie die größten Festplatten im Array
	auswählen, da die Paritätsinformationen auf die Größe der größten
	Datenfestplatte im Array anwachsen können.

	Diese Festplatten werden zum Speichern der `parity`-Dateien
	reserviert. Sie sollten Ihre Daten nicht darauf speichern.

	Anschließend müssen Sie die `data`-Festplatten definieren, die Sie
	mit SnapRAID schützen möchten. Der Schutz ist effektiver, wenn diese
	Festplatten Daten enthalten, die sich selten ändern. Aus diesem Grund
	ist es besser, die Windows C:\-Festplatte oder die Unix-Verzeichnisse
	/home, /var und /tmp NICHT einzuschließen.

	Die Liste der Dateien wird in den `content`-Dateien gespeichert,
	die normalerweise auf den Daten-, Paritäts- oder Boot-Festplatten
	gespeichert werden. Diese Datei enthält die Details Ihrer Sicherung,
	einschließlich aller Prüfsummen zur Überprüfung ihrer Integrität.
	Die `content`-Datei wird in mehreren Kopien gespeichert, und jede Kopie
	muss sich auf einer anderen Festplatte befinden, um sicherzustellen,
	dass selbst bei mehreren Festplattenausfällen mindestens eine Kopie
	verfügbar ist.

	Angenommen, Sie sind nur an einer Paritätsstufe des Schutzes interessiert
	und Ihre Festplatten befinden sich unter:

		:/mnt/diskp <- ausgewählte Festplatte für Parität
		:/mnt/disk1 <- erste zu schützende Festplatte
		:/mnt/disk2 <- zweite zu schützende Festplatte
		:/mnt/disk3 <- dritte zu schützende Festplatte

	Sie müssen die Konfigurationsdatei /etc/snapraid.conf mit den
	folgenden Optionen erstellen:

		:parity /mnt/diskp/snapraid.parity
		:content /var/snapraid/snapraid.content
		:content /mnt/disk1/snapraid.content
		:content /mnt/disk2/snapraid.content
		:data d1 /mnt/disk1/
		:data d2 /mnt/disk2/
		:data d3 /mnt/disk3/

	Wenn Sie Windows verwenden, sollten Sie das Windows-Pfadformat mit
	Laufwerksbuchstaben und Backslashes anstelle von Slashes verwenden.

		:parity E:\snapraid.parity
		:content C:\snapraid\snapraid.content
		:content F:\array\snapraid.content
		:content G:\array\snapraid.content
		:data d1 F:\array\
		:data d2 G:\array\
		:data d3 H:\array\

	Wenn Sie viele Festplatten haben und keine Laufwerksbuchstaben mehr
	verfügbar sind, können Sie Festplatten direkt in Unterordnern
	einhängen (mounten). Siehe:

		:https://www.google.com/search?q=Windows+mount+point

	Zu diesem Zeitpunkt sind Sie bereit, den Befehl `sync` auszuführen,
	um die Paritätsinformationen zu erstellen.

		:snapraid sync

	Dieser Vorgang kann beim ersten Mal mehrere Stunden dauern, abhängig
	von der Größe der bereits auf den Festplatten vorhandenen Daten.
	Wenn die Festplatten leer sind, ist der Vorgang sofort abgeschlossen.

	Sie können ihn jederzeit durch Drücken von Strg+C anhalten, und beim
	nächsten Lauf wird er dort fortgesetzt, wo er unterbrochen wurde.

	Wenn dieser Befehl abgeschlossen ist, sind Ihre Daten SICHER.

	Jetzt können Sie Ihr Array nach Belieben verwenden und die Paritätsinformationen
	regelmäßig durch Ausführen des `sync`-Befehls aktualisieren.

  Scrubbing
	Um die Daten und die Parität regelmäßig auf Fehler zu überprüfen,
	können Sie den Befehl `scrub` ausführen.

		:snapraid scrub

	Dieser Befehl vergleicht die Daten in Ihrem Array mit dem Hash,
	der während des `sync`-Befehls berechnet wurde, um die Integrität
	zu überprüfen.

	Jeder Lauf des Befehls überprüft ungefähr 8% des Arrays,
	ausgenommen Daten, die bereits in den letzten 10 Tagen gescrubbed wurden.
	Sie können die Option -p, --plan verwenden, um eine andere Menge
	anzugeben, und die Option -o, --older-than, um ein anderes Alter
	in Tagen anzugeben. Zum Beispiel, um 5% des Arrays auf Blöcke zu
	überprüfen, die älter als 20 Tage sind, verwenden Sie:

		:snapraid -p 5 -o 20 scrub

	Wenn während des Vorgangs stille oder Eingabe-/Ausgabefehler
	gefunden werden, werden die entsprechenden Blöcke in der
	`content`-Datei als fehlerhaft (bad) markiert und im Befehl
	`status` aufgelistet.

		:snapraid status

	Um sie zu beheben, können Sie den Befehl `fix` verwenden und dabei
	nach fehlerhaften Blöcken mit der Option -e, --filter-error
	filtern:

		:snapraid -e fix

	Beim nächsten `scrub` verschwinden die Fehler aus dem `status`-Bericht,
	wenn sie tatsächlich behoben sind. Um es schneller zu machen,
	können Sie -p bad verwenden, um nur als fehlerhaft markierte Blöcke
	zu scrubben.

		:snapraid -p bad scrub

	Das Ausführen von `scrub` auf einem nicht synchronisierten Array
	kann Fehler melden, die durch entfernte oder geänderte Dateien
	verursacht wurden. Diese Fehler werden in der `scrub`-Ausgabe
	gemeldet, aber die zugehörigen Blöcke werden nicht als fehlerhaft
	markiert.

  Pooling
	Hinweis: Die unten beschriebene Pooling-Funktion wurde durch das
	Tool mergefs ersetzt, das jetzt die empfohlene Option für Linux-Benutzer
	in der SnapRAID-Community ist. Mergefs bietet eine flexiblere
	und effizientere Möglichkeit, mehrere Laufwerke zu einem einzigen,
	vereinheitlichten Einhängepunkt (Mount Point) zusammenzufassen,
	was einen nahtlosen Zugriff auf Dateien über Ihr gesamtes Array
	hinweg ermöglicht, ohne auf symbolische Links angewiesen zu sein.
	Es lässt sich gut mit SnapRAID zum Paritätsschutz integrieren und
	wird häufig in Setups wie OpenMediaVault (OMV) oder benutzerdefinierten
	NAS-Konfigurationen verwendet.

	Um alle Dateien in Ihrem Array im selben Verzeichnisbaum anzuzeigen,
	können Sie die `pooling`-Funktion aktivieren. Sie erstellt eine
	schreibgeschützte virtuelle Ansicht aller Dateien in Ihrem Array
	mithilfe symbolischer Links.

	Sie können das `pooling`-Verzeichnis in der Konfigurationsdatei
	mit folgender Option konfigurieren:

		:pool /pool

	oder, wenn Sie Windows verwenden, mit:

		:pool C:\pool

	und anschließend den Befehl `pool` ausführen, um die virtuelle
	Ansicht zu erstellen oder zu aktualisieren.

		:snapraid pool

	Wenn Sie eine Unix-Plattform verwenden und dieses Verzeichnis über
	das Netzwerk für Windows- oder Unix-Maschinen freigeben möchten,
	sollten Sie die folgenden Optionen zu Ihrer /etc/samba/smb.conf
	hinzufügen:

		:# Im globalen Abschnitt von smb.conf
		:unix extensions = no

		:# Im Freigabe-Abschnitt von smb.conf
		:[pool]
		:comment = Pool
		:path = /pool
		:read only = yes
		:guest ok = yes
		:wide links = yes
		:follow symlinks = yes

	In Windows erfordert die Freigabe symbolischer Links über ein Netzwerk,
	dass Clients diese remote auflösen. Um dies zu ermöglichen,
	müssen Sie neben der Freigabe des Pool-Verzeichnisses auch alle
	Festplatten unabhängig freigeben, indem Sie die in der Konfigurationsdatei
	definierten Festplattennamen als Freigabepunkte verwenden.
	Sie müssen auch in der Option `share` der Konfigurationsdatei
	den Windows UNC-Pfad angeben, den Remote-Clients benötigen, um
	auf diese freigegebenen Festplatten zuzugreifen.

	Zum Beispiel, wenn Sie von einem Server namens `darkstar` aus
	arbeiten, können Sie die Optionen verwenden:

		:data d1 F:\array\
		:data d2 G:\array\
		:data d3 H:\array\
		:pool C:\pool
		:share \\darkstar

	und die folgenden Verzeichnisse über das Netzwerk freigeben:

		:\\darkstar\pool -> C:\pool
		:\\darkstar\d1 -> F:\array
		:\\darkstar\d2 -> G:\array
		:\\darkstar\d3 -> H:\array

	um Remote-Clients den Zugriff auf alle Dateien unter \\darkstar\pool
	zu ermöglichen.

	Möglicherweise müssen Sie auch Remote-Clients konfigurieren, um
	den Zugriff auf Remote-Symlinks mit dem Befehl zu aktivieren:

		:fsutil behavior set SymlinkEvaluation L2L:1 R2R:1 L2R:1 R2L:1

  Undeleting
	SnapRAID funktioniert eher wie ein Sicherungsprogramm als ein RAID-System
	und kann verwendet werden, um Dateien in ihren vorherigen Zustand
	wiederherzustellen oder das Löschen rückgängig zu machen, indem die
	Option -f, --filter verwendet wird:

		:snapraid fix -f DATEI

	oder für ein Verzeichnis:

		:snapraid fix -f VERZEICHNIS/

	Sie können es auch verwenden, um nur versehentlich gelöschte Dateien
	innerhalb eines Verzeichnisses wiederherzustellen, indem Sie die
	Option -m, --filter-missing verwenden, die nur fehlende Dateien
	wiederherstellt und alle anderen unberührt lässt.

		:snapraid fix -m -f VERZEICHNIS/

	Oder um alle gelöschten Dateien auf allen Laufwerken wiederherzustellen mit:

		:snapraid fix -m

  Recovering
	Das Schlimmste ist passiert, und Sie haben eine oder mehrere Festplatten
	verloren!

	KEINE PANIK! Sie werden in der Lage sein, sie wiederherzustellen!

	Das erste, was Sie tun müssen, ist, weitere Änderungen an Ihrem
	Festplatten-Array zu vermeiden. Deaktivieren Sie alle Remote-Verbindungen
	dazu und alle geplanten Prozesse, einschließlich geplanter SnapRAID-nächtlicher
	`sync`- oder `scrub`-Läufe.

	Fahren Sie dann mit den folgenden Schritten fort.

    STEP 1 -> Reconfigure
	Sie benötigen etwas Platz zur Wiederherstellung, idealerweise auf
	zusätzlichen Ersatzfestplatten, aber eine externe USB-Festplatte oder
	eine Remote-Festplatte ist ausreichend.

	Ändern Sie die SnapRAID-Konfigurationsdatei so, dass die `data`-
	oder `parity`-Option der ausgefallenen Festplatte auf einen Ort mit
	genügend leerem Speicherplatz zur Wiederherstellung der Dateien zeigt.

	Wenn zum Beispiel die Festplatte `d1` ausgefallen ist, ändern Sie von:

		:data d1 /mnt/disk1/

	zu:

		:data d1 /mnt/new_spare_disk/

	Wenn die wiederherzustellende Festplatte eine Paritätsfestplatte ist,
	aktualisieren Sie die entsprechende `parity`-Option.
	Wenn Sie mehrere ausgefallene Festplatten haben, aktualisieren Sie
	alle ihre Konfigurationsoptionen.

    STEP 2 -> Fix
	Führen Sie den `fix`-Befehl aus und speichern Sie das Protokoll
	in einer externen Datei mit:

		:snapraid -d NAME -l fix.log fix

	Wobei NAME der Name der Festplatte ist, wie z.B. `d1` in unserem
	vorherigen Beispiel. Wenn die wiederherzustellende Festplatte eine
	Paritätsfestplatte ist, verwenden Sie die Namen `parity`, `2-parity`,
	usw. Wenn Sie mehrere ausgefallene Festplatten haben, verwenden Sie
	mehrere -d-Optionen, um alle anzugeben.

	Dieser Befehl wird lange dauern.

	Stellen Sie sicher, dass Sie einige Gigabyte frei haben, um die
	Datei fix.log zu speichern. Führen Sie ihn von einer Festplatte
	mit ausreichend freiem Speicherplatz aus.

	Jetzt haben Sie alles wiederhergestellt, was wiederherstellbar ist.
	Wenn einige Dateien teilweise oder vollständig nicht wiederherstellbar
	sind, werden sie umbenannt, indem die Erweiterung `.unrecoverable`
	hinzugefügt wird.

	Sie finden eine detaillierte Liste aller nicht wiederherstellbaren
	Blöcke in der Datei fix.log, indem Sie alle Zeilen überprüfen,
	die mit `unrecoverable:` beginnen.

	Wenn Sie mit der Wiederherstellung nicht zufrieden sind, können Sie
	sie so oft wie gewünscht wiederholen.

	Wenn Sie zum Beispiel Dateien nach dem letzten `sync` aus dem Array
	entfernt haben, kann dies dazu führen, dass einige Dateien nicht
	wiederhergestellt werden. In diesem Fall können Sie den `fix`
	mithilfe der Option -i, --import wiederholen, wobei Sie angeben,
	wo sich diese Dateien jetzt befinden, um sie erneut in den
	Wiederherstellungsprozess einzubeziehen.

	Wenn Sie mit der Wiederherstellung zufrieden sind, können Sie
	fortfahren, aber beachten Sie, dass Sie nach der Synchronisierung
	den `fix`-Befehl nicht mehr wiederholen können!

    STEP 3 -> Check
	Als vorsichtige Überprüfung können Sie jetzt einen `check`-Befehl
	ausführen, um sicherzustellen, dass auf der wiederhergestellten
	Festplatte alles korrekt ist.

		:snapraid -d NAME -a check

	Wobei NAME der Name der Festplatte ist, wie z.B. `d1` in unserem
	vorherigen Beispiel.

	Die Optionen -d und -a weisen SnapRAID an, nur die angegebene
	Festplatte zu überprüfen und alle Paritätsdaten zu ignorieren.

	Dieser Befehl wird lange dauern, aber wenn Sie nicht übermäßig
	vorsichtig sind, können Sie ihn überspringen.

    STEP 4 -> Sync
	Führen Sie den `sync`-Befehl aus, um das Array mit der neuen
	Festplatte zu resynchronisieren.

		:snapraid sync

	Wenn alles wiederhergestellt ist, ist dieser Befehl sofort.

Commands
	SnapRAID bietet einige einfache Befehle, mit denen Sie:

	* Den Status des Arrays ausgeben -> `status`
	* Die Festplatten steuern -> `smart`, `probe`, `up`, `down`
	* Eine Sicherung/einen Snapshot erstellen -> `sync`
	* Daten regelmäßig überprüfen -> `scrub`
	* Die letzte Sicherung/den letzten Snapshot wiederherstellen -> `fix`.

	Befehle müssen in Kleinbuchstaben geschrieben werden.

  status
	Gibt eine Zusammenfassung des Zustands des Festplatten-Arrays aus.

	Es enthält Informationen zur Paritätsfragmentierung, wie alt
	die Blöcke sind, ohne Überprüfung, und alle aufgezeichneten
	stillen Fehler, die beim Scrubbing aufgetreten sind.

	Die präsentierten Informationen beziehen sich auf den letzten Zeitpunkt,
	zu dem Sie `sync` ausgeführt haben. Spätere Änderungen werden
	nicht berücksichtigt.

	Wenn fehlerhafte Blöcke erkannt wurden, werden deren Blocknummern
	aufgelistet. Um sie zu beheben, können Sie den Befehl `fix -e`
	verwenden.

	Es zeigt auch ein Diagramm an, das den letzten Zeitpunkt darstellt,
	zu dem jeder Block gescrubbed oder synchronisiert wurde. Gescrubbed
	Blöcke werden mit '*', Blöcke, die synchronisiert, aber noch nicht
	gescrubbed wurden, mit 'o' angezeigt.

	Es wird nichts geändert.

  smart
	Gibt einen SMART-Bericht aller Festplatten im System aus.

	Es enthält eine Schätzung der Ausfallwahrscheinlichkeit im nächsten
	Jahr, sodass Sie die Wartungsersetzung von Festplatten planen können,
	die verdächtige Attribute aufweisen.

	Diese Wahrscheinlichkeitsschätzung wird durch Korrelation der SMART-Attribute
	der Festplatten mit den Backblaze-Daten erhalten, die unter
	folgender Adresse verfügbar sind:

		:https://www.backblaze.com/hard-drive-test-data.html

	Wenn SMART meldet, dass eine Festplatte ausfällt, wird `FAIL`
	oder `PREFAIL` für diese Festplatte gedruckt, und SnapRAID
	endet mit einem Fehler. In diesem Fall wird ein sofortiger
	Austausch der Festplatte dringend empfohlen.

	Weitere mögliche Statuszeichenfolgen sind:
		logfail - In der Vergangenheit lagen einige Attribute unter
			dem Schwellenwert.
		logerr - Das Gerätefehlerprotokoll enthält Fehler.
		selferr - Das Geräte-Selbsttestprotokoll enthält Fehler.

	Wenn die Option -v, --verbose angegeben ist, wird eine tiefere
	statistische Analyse bereitgestellt. Diese Analyse kann Ihnen bei
	der Entscheidung helfen, ob Sie mehr oder weniger Parität benötigen.

	Dieser Befehl verwendet das `smartctl`-Tool und entspricht dem
	Ausführen von `smartctl -a` auf allen Geräten.

	Wenn Ihre Geräte nicht korrekt automatisch erkannt werden, können Sie
	einen benutzerdefinierten Befehl mithilfe der `smartctl`-Option
	in der Konfigurationsdatei angeben.

	Es wird nichts geändert.

  probe
	Gibt den POWER-Zustand aller Festplatten im System aus.

	`Standby` bedeutet, dass die Festplatte nicht dreht. `Active`
	bedeutet, dass die Festplatte dreht.

	Dieser Befehl verwendet das `smartctl`-Tool und entspricht dem
	Ausführen von `smartctl -n standby -i` auf allen Geräten.

	Wenn Ihre Geräte nicht korrekt automatisch erkannt werden, können Sie
	einen benutzerdefinierten Befehl mithilfe der `smartctl`-Option
	in der Konfigurationsdatei angeben.

	Es wird nichts geändert.

  up
	Dreht alle Festplatten des Arrays hoch (spins up).

	Sie können nur bestimmte Festplatten mithilfe der Option -d, --filter-disk
	hochdrehen.

	Das gleichzeitige Hochdrehen aller Festplatten erfordert viel Strom.
	Stellen Sie sicher, dass Ihr Netzteil dies aushalten kann.

	Es wird nichts geändert.

  down
	Dreht alle Festplatten des Arrays herunter (spins down).

	Dieser Befehl verwendet das `smartctl`-Tool und entspricht dem
	Ausführen von `smartctl -s standby,now` auf allen Geräten.

	Sie können nur bestimmte Festplatten mithilfe der Option -d, --filter-disk
	herunterdrehen.

	Um automatisch bei einem Fehler herunterzufahren, können Sie die Option
	-s, --spin-down-on-error mit jedem anderen Befehl verwenden, was dem
	manuellen Ausführen von `down` bei Auftreten eines Fehlers
	entspricht.

	Es wird nichts geändert.

  diff
	Listet alle Dateien auf, die seit dem letzten `sync` geändert wurden
	und deren Paritätsdaten neu berechnet werden müssen.

	Dieser Befehl überprüft nicht die Dateidaten, sondern nur den
	Dateizeitstempel, die Größe und die Inode.

	Nach dem Auflisten aller geänderten Dateien wird eine Zusammenfassung
	der Änderungen präsentiert, gruppiert nach:
		equal - Dateien, die sich seitdem nicht geändert haben.
		added - Neu hinzugefügte Dateien, die vorher nicht vorhanden waren.
		removed - Entfernte Dateien.
		updated - Dateien mit einer anderen Größe oder einem anderen
			Zeitstempel, was bedeutet, dass sie geändert wurden.
		moved - Auf derselben Festplatte in ein anderes Verzeichnis
			verschobene Dateien. Sie werden durch denselben Namen,
			dieselbe Größe, denselben Zeitstempel und dieselbe Inode,
			aber ein anderes Verzeichnis identifiziert.
		copied - Auf dieselbe oder eine andere Festplatte kopierte
			Dateien. Beachten Sie, dass wenn sie tatsächlich auf eine
			andere Festplatte verschoben werden, sie auch unter `removed`
			gezählt werden. Sie werden durch denselben Namen, dieselbe
			Größe und denselben Zeitstempel identifiziert. Wenn der
			Untersekunden-Zeitstempel Null ist, muss der vollständige
			Pfad übereinstimmen, nicht nur der Name.
		restored - Dateien mit einer anderen Inode, aber übereinstimmendem
			Namen, Größe und Zeitstempel. Dies sind normalerweise
			Dateien, die nach dem Löschen wiederhergestellt wurden.

	Wenn ein `sync` erforderlich ist, ist der Rückgabecode des Prozesses 2
	anstelle des Standardwerts 0. Der Rückgabecode 1 wird für eine
	allgemeine Fehlerbedingung verwendet.

	Es wird nichts geändert.

  sync
	Aktualisiert die Paritätsinformationen. Alle geänderten Dateien
	im Festplatten-Array werden gelesen, und die entsprechenden
	Paritätsdaten werden aktualisiert.

	Sie können diesen Vorgang jederzeit durch Drücken von Strg+C
	anhalten, ohne die bereits geleistete Arbeit zu verlieren.
	Beim nächsten Lauf wird der `sync`-Vorgang dort fortgesetzt,
	wo er unterbrochen wurde.

	Wenn während des Vorgangs stille oder Eingabe-/Ausgabefehler
	gefunden werden, werden die entsprechenden Blöcke als fehlerhaft
	markiert.

	Dateien werden durch Pfad und/oder Inode identifiziert und anhand
	von Größe und Zeitstempel überprüft.
	Wenn die Dateigröße oder der Zeitstempel abweicht, werden die Paritätsdaten
	für die gesamte Datei neu berechnet.
	Wenn die Datei auf derselben Festplatte verschoben oder umbenannt
	wird und dieselbe Inode beibehält, wird die Parität nicht neu
	berechnet.
	Wenn die Datei auf eine andere Festplatte verschoben wird, wird die
	Parität neu berechnet, aber die zuvor berechneten Hash-Informationen
	werden beibehalten.

	Die `content`- und `parity`-Dateien werden bei Bedarf geändert.
	Die Dateien im Array werden NICHT geändert.

  scrub
	Scrubbt das Array und überprüft auf stille oder Eingabe-/Ausgabefehler
	auf Daten- und Paritätsfestplatten.

	Jede Ausführung überprüft ungefähr 8% des Arrays, ausgenommen Daten,
	die bereits in den letzten 10 Tagen gescrubbed wurden.
	Dies bedeutet, dass Scrubbing einmal pro Woche sicherstellt, dass
	jedes Bit an Daten mindestens einmal alle drei Monate überprüft wird.

	Sie können einen anderen Scrub-Plan oder eine andere Menge mithilfe
	der Option -p, --plan definieren, die Folgendes akzeptiert:
	bad - Scrubbt als fehlerhaft markierte Blöcke.
	new - Scrubbt gerade synchronisierte Blöcke, die noch nicht gescrubbed
		wurden.
	full - Scrubbt alles.
	0-100 - Scrubbt den angegebenen Prozentsatz der Blöcke.

	Wenn Sie einen Prozentsatz angeben, können Sie auch die Option
	-o, --older-than verwenden, um festzulegen, wie alt der Block sein
	soll. Die ältesten Blöcke werden zuerst gescrubbed, um eine optimale
	Überprüfung zu gewährleisten. Wenn Sie nur die gerade synchronisierten
	Blöcke scrubben möchten, die noch nicht gescrubbed wurden,
	verwenden Sie die Option `-p new`.

	Um Details zum Scrub-Status zu erhalten, verwenden Sie den Befehl
	`status`.

	Für jeden gefundenen stillen oder Eingabe-/Ausgabefehler werden die
	entsprechenden Blöcke in der `content`-Datei als fehlerhaft (bad)
	markiert. Diese fehlerhaften Blöcke werden in `status` aufgelistet
	und können mit `fix -e` behoben werden. Nach der Behebung werden
	sie beim nächsten Scrub erneut überprüft, und wenn sie als korrigiert
	befunden werden, wird die fehlerhafte Markierung entfernt.
	Um nur die fehlerhaften Blöcke zu scrubben, können Sie den Befehl
	`scrub -p bad` verwenden.

	Es wird empfohlen, `scrub` nur auf einem synchronisierten Array
	auszuführen, um gemeldete Fehler zu vermeiden, die durch nicht
	synchronisierte Daten verursacht werden. Diese Fehler werden als
	keine stillen Fehler erkannt, und die Blöcke werden nicht als fehlerhaft
	markiert, aber solche Fehler werden in der Ausgabe des Befehls
	gemeldet.

	Die `content`-Datei wird geändert, um die Zeit der letzten
	Überprüfung für jeden Block zu aktualisieren und fehlerhafte
	Blöcke zu markieren.
	Die `parity`-Dateien werden NICHT geändert.
	Die Dateien im Array werden NICHT geändert.

  fix
	Behebt alle Dateien und die Paritätsdaten.

	Alle Dateien und Paritätsdaten werden mit dem Snapshot-Zustand
	verglichen, der beim letzten `sync` gespeichert wurde.
	Wenn eine Abweichung gefunden wird, wird sie auf den gespeicherten
	Snapshot zurückgesetzt.

	WARNUNG! Der Befehl `fix` unterscheidet nicht zwischen Fehlern und
	beabsichtigten Änderungen. Er setzt den Dateizustand bedingungslos
	auf den letzten `sync` zurück.

	Wenn keine andere Option angegeben ist, wird das gesamte Array verarbeitet.
	Verwenden Sie die Filteroptionen, um eine Teilmenge von Dateien
	oder Festplatten für die Operation auszuwählen.

	Um nur die Blöcke zu beheben, die während `sync` und `scrub` als
	fehlerhaft markiert wurden, verwenden Sie die Option -e, --filter-error.
	Im Gegensatz zu anderen Filteroptionen wendet diese Behebungen nur
	auf Dateien an, die seit dem letzten `sync` unverändert sind.

	SnapRAID benennt alle Dateien, die nicht behoben werden können,
	durch Hinzufügen der Erweiterung `.unrecoverable` um.

	Vor dem Beheben wird das gesamte Array gescannt, um alle Dateien
	zu finden, die seit der letzten `sync`-Operation verschoben wurden.
	Diese Dateien werden anhand ihres Zeitstempels identifiziert, wobei
	Name und Verzeichnis ignoriert werden, und werden bei Bedarf im
	Wiederherstellungsprozess verwendet. Wenn Sie einige davon außerhalb
	des Arrays verschoben haben, können Sie die Option -i, --import
	verwenden, um zusätzliche Verzeichnisse zum Scannen anzugeben.

	Dateien werden nur anhand des Pfads identifiziert, nicht anhand
	der Inode.

	Die `content`-Datei wird NICHT geändert.
	Die `parity`-Dateien werden bei Bedarf geändert.
	Die Dateien im Array werden bei Bedarf geändert.

  check
	Überprüft alle Dateien und die Paritätsdaten.

	Es funktioniert wie `fix`, simuliert jedoch nur eine Wiederherstellung,
	und es werden keine Änderungen in das Array geschrieben.

	Dieser Befehl ist primär für manuelle Überprüfung gedacht, wie z.B.
	nach einem Wiederherstellungsprozess oder unter anderen speziellen
	Bedingungen. Für regelmäßige und geplante Überprüfungen verwenden
	Sie `scrub`.

	Wenn Sie die Option -a, --audit-only verwenden, wird nur der Hash
	der Dateien überprüft, und die Paritätsdaten werden für einen
	schnelleren Lauf ignoriert.

	Dateien werden nur anhand des Pfads identifiziert, nicht anhand
	der Inode.

	Es wird nichts geändert.

  list
	Listet alle Dateien auf, die sich zum Zeitpunkt des letzten `sync`
	im Array befanden.

	Mit -v oder --verbose wird auch die Untersekundenzeit angezeigt.

	Es wird nichts geändert.

  dup
	Listet alle doppelten Dateien auf. Zwei Dateien gelten als gleich,
	wenn ihre Hashes übereinstimmen. Die Dateidaten werden nicht gelesen;
	es werden nur die vorab berechneten Hashes verwendet.

	Es wird nichts geändert.

  pool
	Erstellt oder aktualisiert eine virtuelle Ansicht aller
	Dateien in Ihrem Festplatten-Array im `pooling`-Verzeichnis.

	Die Dateien werden nicht kopiert, sondern mithilfe symbolischer
	Links verknüpft.

	Beim Aktualisieren werden alle vorhandenen symbolischen Links und
	leeren Unterverzeichnisse gelöscht und durch die neue Ansicht des
	Arrays ersetzt. Alle anderen regulären Dateien bleiben erhalten.

	Außerhalb des Pool-Verzeichnisses wird nichts geändert.

  devices
	Gibt die Low-Level-Geräte aus, die vom Array verwendet werden.

	Dieser Befehl zeigt die Gerätezuordnungen im Array an und ist
	hauptsächlich als Skriptschnittstelle gedacht.

	Die ersten beiden Spalten sind die Low-Level-Geräte-ID und der Pfad.
	Die nächsten beiden Spalten sind die High-Level-Geräte-ID und der Pfad.
	Die letzte Spalte ist der Festplattenname im Array.

	In den meisten Fällen haben Sie ein Low-Level-Gerät für jede
	Festplatte im Array, aber in einigen komplexeren Konfigurationen
	können mehrere Low-Level-Geräte von einer einzigen Festplatte
	im Array verwendet werden.

	Es wird nichts geändert.

  touch
	Setzt einen beliebigen Untersekunden-Zeitstempel für alle Dateien,
	deren Untersekunden-Zeitstempel auf Null gesetzt ist.

	Dies verbessert die Fähigkeit von SnapRAID, verschobene und
	kopierte Dateien zu erkennen, da es den Zeitstempel fast
	eindeutig macht und mögliche Duplikate reduziert.

	Genauer gesagt, wenn der Untersekunden-Zeitstempel nicht Null ist,
	wird eine verschobene oder kopierte Datei als solche identifiziert,
	wenn sie mit Name, Größe und Zeitstempel übereinstimmt. Wenn der
	Untersekunden-Zeitstempel Null ist, wird sie nur als Kopie betrachtet,
	wenn der vollständige Pfad, die Größe und der Zeitstempel alle
	übereinstimmen.

	Der Zeitstempel mit Sekundenpräzision wird nicht geändert,
	sodass alle Daten und Zeiten Ihrer Dateien erhalten bleiben.

  rehash
	Plant eine Neu-Hash-Berechnung (rehash) des gesamten Arrays.

	Dieser Befehl ändert die verwendete Hash-Art, typischerweise beim
	Upgrade von einem 32-Bit-System auf ein 64-Bit-System, um von
	MurmurHash3 auf den schnelleren SpookyHash umzuschalten.

	Wenn Sie bereits den optimalen Hash verwenden, tut dieser Befehl
	nichts und informiert Sie darüber, dass keine Aktion erforderlich ist.

	Die Neu-Hash-Berechnung wird nicht sofort durchgeführt, sondern
	findet schrittweise während `sync` und `scrub` statt.

	Sie können den Neu-Hash-Zustand mithilfe von `status` überprüfen.

	Während der Neu-Hash-Berechnung behält SnapRAID die volle
	Funktionalität bei, mit der einzigen Ausnahme, dass `dup` keine
	doppelten Dateien mit einem anderen Hash erkennen kann.

Options
	SnapRAID bietet die folgenden Optionen:

	-c, --conf CONFIG
		Wählt die zu verwendende Konfigurationsdatei aus. Wenn nicht
		angegeben, wird unter Unix die Datei `/usr/local/etc/snapraid.conf`
		verwendet, falls sie existiert, andernfalls `/etc/snapraid.conf`.
		Unter Windows wird die Datei `snapraid.conf` im selben
		Verzeichnis wie `snapraid.exe` verwendet.

	-f, --filter PATTERN
		Filtert die zu verarbeitenden Dateien in `check` und `fix`.
		Nur die Dateien, die dem angegebenen Muster entsprechen, werden
		verarbeitet. Diese Option kann mehrmals verwendet werden.
		Weitere Details zu Musterspezifikationen finden Sie im
		Abschnitt PATTERN. Unter Unix stellen Sie sicher, dass Globbing-Zeichen
		in Anführungszeichen gesetzt werden, wenn sie verwendet werden.
		Diese Option kann nur mit `check` und `fix` verwendet werden.
		Sie kann nicht mit `sync` und `scrub` verwendet werden, da
		diese immer das gesamte Array verarbeiten.

	-d, --filter-disk NAME
		Filtert die zu verarbeitenden Festplatten in `check`, `fix`, `up`
		und `down`. Sie müssen einen Festplattennamen angeben, wie
		in der Konfigurationsdatei definiert. Sie können auch Paritätsfestplatten
		mit den Namen `parity`, `2-parity`, `3-parity`, usw. angeben,
		um Vorgänge auf eine bestimmte Paritätsfestplatte zu beschränken.
		Wenn Sie mehrere --filter, --filter-disk und --filter-missing
		Optionen kombinieren, werden nur Dateien ausgewählt, die allen
		Filtern entsprechen. Diese Option kann mehrmals verwendet werden.
		Diese Option kann nur mit `check`, `fix`, `up` und `down` verwendet
		werden. Sie kann nicht mit `sync` und `scrub` verwendet werden,
		da diese immer das gesamte Array verarbeiten.

	-m, --filter-missing
		Filtert die zu verarbeitenden Dateien in `check` und `fix`.
		Nur die Dateien, die im Array fehlen oder gelöscht wurden, werden
		verarbeitet. Bei Verwendung mit `fix` fungiert dies als
		`undelete`-Befehl (Wiederherstellung von Gelöschtem).
		Wenn Sie mehrere --filter, --filter-disk und --filter-missing
		Optionen kombinieren, werden nur Dateien ausgewählt, die allen
		Filtern entsprechen. Diese Option kann nur mit `check` und `fix`
		verwendet werden. Sie kann nicht mit `sync` und `scrub`
		verwendet werden, da diese immer das gesamte Array verarbeiten.

	-e, --filter-error
		Verarbeitet die Dateien mit Fehlern in `check` und `fix`.
		Es verarbeitet nur Dateien, deren Blöcke während `sync`
		und `scrub` als mit stillen oder Eingabe-/Ausgabefehlern
		markiert wurden, wie in `status` aufgelistet.
		Diese Option kann nur mit `check` und `fix` verwendet werden.

	-p, --plan PERC|bad|new|full
		Wählt den Scrub-Plan aus. Wenn PERC ein numerischer Wert
		von 0 bis 100 ist, wird er als Prozentsatz der zu scrubbenden
		Blöcke interpretiert. Anstelle eines Prozentsatzes können Sie
		einen Plan angeben: `bad` scrubbt fehlerhafte Blöcke,
		`new` scrubbt Blöcke, die noch nicht gescrubbed wurden,
		und `full` scrubbt alles.
		Diese Option kann nur mit `scrub` verwendet werden.

	-o, --older-than DAYS
		Wählt den ältesten Teil des Arrays aus, der in `scrub`
		verarbeitet werden soll. DAYS ist das Mindestalter in Tagen,
		damit ein Block gescrubbed werden kann; der Standardwert
		ist 10. Blöcke, die als fehlerhaft markiert sind, werden
		immer gescrubbed, unabhängig von dieser Option.
		Diese Option kann nur mit `scrub` verwendet werden.

	-a, --audit-only
		Überprüft in `check` den Hash der Dateien, ohne die Paritätsdaten
		zu überprüfen. Wenn Sie nur an der Überprüfung der Dateidaten
		interessiert sind, kann diese Option den Überprüfungsprozess
		erheblich beschleunigen.
		Diese Option kann nur mit `check` verwendet werden.

	-h, --pre-hash
		Führt in `sync` eine vorläufige Hash-Phase aller neuen Daten
		zur zusätzlichen Überprüfung vor der Paritätsberechnung durch.
		Normalerweise wird in `sync` keine vorläufige Hash-Berechnung
		durchgeführt, und die neuen Daten werden unmittelbar vor der
		Paritätsberechnung gehasht, wenn sie zum ersten Mal gelesen werden.
		Dieser Vorgang tritt auf, wenn das System unter starker Last
		steht, wobei alle Festplatten drehen und eine ausgelastete CPU
		vorliegt. Dies ist ein Extremzustand für die Maschine, und wenn
		sie ein latentes Hardwareproblem hat, können stille Fehler
		unentdeckt bleiben, da die Daten noch nicht gehasht sind.
		Um dieses Risiko zu vermeiden, können Sie den `pre-hash`-Modus
		aktivieren, um alle Daten zweimal lesen zu lassen und so ihre
		Integrität sicherzustellen. Diese Option überprüft auch im Array
		verschobene Dateien, um sicherzustellen, dass die Verschiebeoperation
		erfolgreich war, und ermöglicht es Ihnen, bei Bedarf eine
		`fix`-Operation durchzuführen, bevor Sie fortfahren.
		Diese Option kann nur mit `sync` verwendet werden.

	-i, --import DIR
		Importiert aus dem angegebenen Verzeichnis alle Dateien, die
		nach dem letzten `sync` aus dem Array gelöscht wurden.
		Wenn Sie solche Dateien noch haben, können sie von `check` und
		`fix` verwendet werden, um den Wiederherstellungsprozess zu
		verbessern. Die Dateien werden gelesen, auch in Unterverzeichnissen,
		und unabhängig von ihrem Namen identifiziert.
		Diese Option kann nur mit `check` und `fix` verwendet werden.

	-s, --spin-down-on-error
		Dreht bei jedem Fehler alle verwalteten Festplatten herunter,
		bevor mit einem von Null verschiedenen Statuscode beendet wird.
		Dies verhindert, dass die Laufwerke nach einem abgebrochenen
		Vorgang aktiv bleiben und drehen, was unnötige Hitzeentwicklung
		und Stromverbrauch vermeiden hilft. Verwenden Sie diese Option,
		um sicherzustellen, dass die Festplatten auch bei einem
		Befehlsfehler sicher gestoppt werden.

	-w, --bw-limit RATE
		Wendet eine globale Bandbreitenbegrenzung für alle Festplatten
		an. RATE ist die Anzahl der Bytes pro Sekunde. Sie können
		einen Multiplikator wie K, M oder G angeben (z.B. --bw-limit 1G).

	-A, --stats
		Aktiviert eine erweiterte Statusansicht, die zusätzliche
		Informationen anzeigt. Der Bildschirm zeigt zwei Diagramme an:
		Das erste Diagramm zeigt die Anzahl der gepufferten Stripes für jede
		Festplatte sowie den Dateipfad der Datei, auf die derzeit auf dieser
		Festplatte zugegriffen wird. Typischerweise hat die langsamste Festplatte
		keinen verfügbaren Puffer, was die maximal erreichbare Bandbreite bestimmt.
		Das zweite Diagramm zeigt den Prozentsatz der Wartezeit über die
		letzten 100 Sekunden. Es wird erwartet, dass die langsamste Festplatte
		die meiste Wartezeit verursacht, während andere Festplatten wenig
		oder keine Wartezeit haben sollten, da sie ihre gepufferten Stripes
		verwenden können. Dieses Diagramm zeigt auch die Zeit an, die
		mit Warten auf Hash-Berechnungen und RAID-Berechnungen verbracht
		wurde. Alle Berechnungen laufen parallel zu den Festplattenoperationen.
		Solange für mindestens eine Festplatte eine messbare Wartezeit
		vorhanden ist, deutet dies daher darauf hin, dass die CPU schnell
		genug ist, um mit der Arbeitslast Schritt zu halten.

	-Z, --force-zero
		Erzwingt den unsicheren Vorgang der Synchronisierung einer Datei
		mit Nullgröße, die zuvor nicht Null war. Wenn SnapRAID eine
		solche Bedingung erkennt, stoppt es den Vorgang, es sei denn,
		Sie geben diese Option an. Dies ermöglicht es Ihnen, leicht zu
		erkennen, wann nach einem Systemabsturz einige zugegriffene Dateien
		abgeschnitten (truncated) wurden. Dies ist eine mögliche Bedingung
		unter Linux mit den ext3/ext4-Dateisystemen.
		Diese Option kann nur mit `sync` verwendet werden.

	-E, --force-empty
		Erzwingt den unsicheren Vorgang der Synchronisierung einer
		Festplatte, bei der alle ursprünglichen Dateien fehlen.
		Wenn SnapRAID erkennt, dass alle ursprünglich auf der Festplatte
		vorhandenen Dateien fehlen oder überschrieben wurden, stoppt es
		den Vorgang, es sei denn, Sie geben diese Option an.
		Dies ermöglicht es Ihnen, leicht zu erkennen, wann ein Daten-Dateisystem
		nicht gemountet (eingehängt) ist.
		Diese Option kann nur mit `sync` verwendet werden.

	-U, --force-uuid
		Erzwingt den unsicheren Vorgang der Synchronisierung, Überprüfung
		und Behebung mit Festplatten, deren UUID geändert wurde.
		Wenn SnapRAID erkennt, dass einige Festplatten ihre UUID
		geändert haben, stoppt es den Vorgang, es sei denn, Sie geben
		diese Option an. Dies ermöglicht es Ihnen, zu erkennen, wann
		Ihre Festplatten an den falschen Einhängepunkten gemountet sind.
		Es ist jedoch erlaubt, eine einzelne UUID-Änderung mit einfacher
		Parität zu haben und mehr mit mehrfacher Parität, da dies
		der normale Fall beim Austausch von Festplatten nach einer
		Wiederherstellung ist.
		Diese Option kann nur mit `sync`, `check` oder `fix` verwendet
		werden.

	-D, --force-device
		Erzwingt den unsicheren Vorgang der Behebung mit unzugänglichen
		Festplatten oder mit Festplatten auf demselben physischen Gerät.
		Wenn Sie beispielsweise zwei Datenfestplatten verloren haben und
		eine Ersatzfestplatte haben, um nur die erste wiederherzustellen,
		können Sie die zweite unzugängliche Festplatte ignorieren.
		Oder wenn Sie eine Festplatte in dem auf einer bereits
		verwendeten Festplatte verbleibenden freien Speicherplatz
		wiederherstellen möchten, wobei sie dasselbe physische Gerät
		gemeinsam nutzen.
		Diese Option kann nur mit `fix` verwendet werden.

	-N, --force-nocopy
		Deaktiviert in `sync`, `check` und `fix` die Kopiererkennungsheuristik.
		Ohne diese Option geht SnapRAID davon aus, dass Dateien mit den
		gleichen Attributen, wie Name, Größe und Zeitstempel, Kopien mit
		denselben Daten sind. Dies ermöglicht die Identifizierung von
		kopierten oder von einer Festplatte auf eine andere verschobenen
		Dateien und die Wiederverwendung der bereits berechneten Hash-Informationen,
		um stille Fehler zu erkennen oder fehlende Dateien wiederherzustellen.
		In einigen seltenen Fällen kann dieses Verhalten zu falsch positiven
		Ergebnissen oder einem langsamen Vorgang aufgrund vieler Hash-Überprüfungen
		führen, und diese Option ermöglicht es Ihnen, solche Probleme
		zu beheben.
		Diese Option kann nur mit `sync`, `check` und `fix` verwendet
		werden.

	-F, --force-full
		Erzwingt in `sync` eine vollständige Neuberechnung der Parität.
		Diese Option kann verwendet werden, wenn Sie eine neue Paritätsstufe
		hinzufügen oder wenn Sie zu einer alten Content-Datei mit
		neueren Paritätsdaten zurückgekehrt sind. Anstatt die Parität
		von Grund auf neu zu erstellen, ermöglicht dies die Wiederverwendung
		der in der Content-Datei vorhandenen Hashes zur Validierung
		von Daten und zur Aufrechterhaltung des Datenschutzes während
		des `sync`-Vorgangs unter Verwendung der vorhandenen Paritätsdaten.
		Diese Option kann nur mit `sync` verwendet werden.

	-R, --force-realloc
		Erzwingt in `sync` eine vollständige Neuallokation von Dateien
		und eine Wiederherstellung der Parität.
		Diese Option kann verwendet werden, um alle Dateien vollständig
		neu zuzuweisen und die Fragmentierung zu entfernen, während
		die in der Content-Datei vorhandenen Hashes zur Validierung
		von Daten wiederverwendet werden.
		Diese Option kann nur mit `sync` verwendet werden.
		WARNUNG! Diese Option ist nur für Experten gedacht, und es wird
		dringend empfohlen, sie nicht zu verwenden.
		Sie haben während der `sync`-Operation KEINEN Datenschutz.

	-l, --log FILE
		Schreibt ein detailliertes Protokoll in die angegebene Datei.
		Wenn diese Option nicht angegeben ist, werden unerwartete Fehler
		auf dem Bildschirm ausgegeben, was bei vielen Fehlern möglicherweise
		zu einer übermäßigen Ausgabe führt. Wenn -l, --log angegeben
		ist, werden nur schwerwiegende Fehler, die SnapRAID zum Stoppen
		bringen, auf dem Bildschirm ausgegeben.
		Wenn der Pfad mit '>>' beginnt, wird die Datei im Anhängemodus
		(append mode) geöffnet. Vorkommen von '%D' und '%T' im Namen
		werden durch Datum und Uhrzeit im Format YYYYMMDD bzw. HHMMSS
		ersetzt. In Windows-Batch-Dateien müssen Sie das '%'-Zeichen
		verdoppeln, z.B. result-%%D.log. Um '>>' zu verwenden, müssen
		Sie den Namen in Anführungszeichen setzen, z.B. `">>result.log"`.
		Um das Protokoll an die Standardausgabe oder den Standardfehler
		auszugeben, können Sie `">&1"` bzw. `">&2"` verwenden.
		Beschreibungen der Protokoll-Tags finden Sie in der Datei
		snapraid_log.txt oder in der Manpage.

	-L, --error-limit NUMBER
		Legt ein neues Fehlerlimit fest, bevor die Ausführung gestoppt
		wird. Standardmäßig stoppt SnapRAID, wenn es mehr als 100
		Eingabe-/Ausgabefehler feststellt, was darauf hindeutet, dass
		eine Festplatte wahrscheinlich ausfällt. Diese Option wirkt sich
		auf `sync` und `scrub` aus, denen es gestattet ist, nach dem
		ersten Satz von Festplattenfehlern fortzufahren, um ihre Vorgänge
		abzuschließen. `check` und `fix` stoppen jedoch immer beim
		ersten Fehler.

	-S, --start BLKSTART
		Startet die Verarbeitung ab der angegebenen Blocknummer.
		Dies kann nützlich sein, um die Überprüfung oder Behebung
		spezifischer Blöcke im Falle einer beschädigten Festplatte
		zu wiederholen. Diese Option ist hauptsächlich für die erweiterte
		manuelle Wiederherstellung gedacht.

	-B, --count BLKCOUNT
		Verarbeitet nur die angegebene Anzahl von Blöcken.
		Diese Option ist hauptsächlich für die erweiterte manuelle
		Wiederherstellung gedacht.

	-C, --gen-conf CONTENT
		Generiert eine Dummy-Konfigurationsdatei aus einer vorhandenen
		Content-Datei. Die Konfigurationsdatei wird in die Standardausgabe
		geschrieben und überschreibt keine vorhandene.
		Diese Konfigurationsdatei enthält auch die Informationen,
		die zur Rekonstruktion der Festplatten-Einhängepunkte erforderlich
		sind, falls Sie das gesamte System verlieren.

	-v, --verbose
		Gibt mehr Informationen auf dem Bildschirm aus.
		Wenn einmal angegeben, werden ausgeschlossene Dateien
		und zusätzliche Statistiken gedruckt.
		Diese Option hat keine Auswirkung auf die Protokolldateien.

	-q, --quiet
		Gibt weniger Informationen auf dem Bildschirm aus.
		Wenn einmal angegeben, wird die Fortschrittsleiste entfernt;
		zweimal, die laufenden Operationen; dreimal, die Info-Meldungen;
		viermal, die Statusmeldungen.
		Schwerwiegende Fehler werden immer auf dem Bildschirm ausgegeben.
		Diese Option hat keine Auswirkung auf die Protokolldateien.

	-H, --help
		Gibt eine kurze Hilfeanzeige aus.

	-V, --version
		Gibt die Programmversion aus.

Configuration
	SnapRAID benötigt eine Konfigurationsdatei, um zu wissen, wo sich
	Ihr Festplatten-Array befindet und wo die Paritätsinformationen
	gespeichert werden sollen.

	Unter Unix wird die Datei `/usr/local/etc/snapraid.conf` verwendet,
	falls sie existiert, andernfalls `/etc/snapraid.conf`.
	Unter Windows wird die Datei `snapraid.conf` im selben
	Verzeichnis wie `snapraid.exe` verwendet.

	Sie muss die folgenden Optionen (Groß- und Kleinschreibung beachten)
	enthalten:

  parity FILE [,FILE] ...
	Definiert die Dateien, die zum Speichern der Paritätsinformationen
	verwendet werden sollen. Die Parität ermöglicht den Schutz
	vor einem einzelnen Festplattenausfall, ähnlich wie bei RAID5.

	Sie können mehrere Dateien angeben, die sich auf unterschiedlichen
	Festplatten befinden müssen. Wenn eine Datei nicht mehr wachsen
	kann, wird die nächste verwendet. Der gesamte verfügbare Platz
	muss mindestens so groß sein wie die größte Datenfestplatte im
	Array.

	Sie können später zusätzliche Paritätsdateien hinzufügen, aber Sie
	können sie nicht neu anordnen oder entfernen.

	Das Reservieren der Paritätsfestplatten für Parität stellt sicher,
	dass sie nicht fragmentiert werden, was die Leistung verbessert.

	Unter Windows werden 256 MB auf jeder Festplatte ungenutzt gelassen,
	um die Warnung vor vollen Festplatten zu vermeiden.

	Diese Option ist obligatorisch und kann nur einmal verwendet werden.

  (2,3,4,5,6)-parity FILE [,FILE] ...
	Definiert die Dateien, die zum Speichern zusätzlicher Paritätsinformationen
	verwendet werden sollen.

	Für jede angegebene Paritätsstufe wird eine zusätzliche Schutzstufe
	aktiviert:

	* 2-parity aktiviert RAID6 Dual-Parität.
	* 3-parity aktiviert Triple-Parität.
	* 4-parity aktiviert Quad- (vierfache) Parität.
	* 5-parity aktiviert Penta- (fünffache) Parität.
	* 6-parity aktiviert Hexa- (sechsfache) Parität.

	Jede Paritätsstufe erfordert die Anwesenheit aller vorherigen
	Paritätsstufen.

	Die gleichen Überlegungen wie bei der Option 'parity' gelten.

	Diese Optionen sind optional und können nur einmal verwendet werden.

  z-parity FILE [,FILE] ...
	Definiert eine alternative Datei und ein alternatives Format zum
	Speichern von Triple-Parität.

	Diese Option ist eine Alternative zu '3-parity', die primär für
	Low-End-CPUs wie ARM oder AMD Phenom, Athlon und Opteron gedacht
	ist, die den SSSE3-Befehlssatz nicht unterstützen. In solchen Fällen
	bietet sie eine bessere Leistung.

	Dieses Format ähnelt dem von ZFS RAIDZ3, ist aber schneller.
	Wie ZFS funktioniert es nicht jenseits der Triple-Parität.

	Bei Verwendung von '3-parity' werden Sie gewarnt, wenn das
	'z-parity'-Format zur Leistungsverbesserung empfohlen wird.

	Es ist möglich, von einem Format in ein anderes zu konvertieren,
	indem die Konfigurationsdatei mit der gewünschten z-parity- oder
	3-parity-Datei angepasst und `fix` zur Neuerstellung verwendet wird.

  content FILE
	Definiert die Datei, die zum Speichern der Liste und Prüfsummen
	aller im Festplatten-Array vorhandenen Dateien verwendet werden soll.

	Sie kann auf einer für Daten, Parität oder einer anderen
	verfügbaren Festplatte platziert werden.
	Wenn Sie eine Datenfestplatte verwenden, wird diese Datei automatisch
	vom `sync`-Vorgang ausgeschlossen.

	Diese Option ist obligatorisch und kann mehrmals verwendet werden,
	um mehrere Kopien derselben Datei zu speichern.

	Sie müssen mindestens eine Kopie für jede verwendete Paritätsfestplatte
	plus eine speichern. Die Verwendung zusätzlicher Kopien schadet nicht.

  data NAME DIR
	Definiert den Namen und den Einhängepunkt (Mount Point) der
	Datenfestplatten im Array. NAME wird zur Identifizierung der
	Festplatte verwendet und muss eindeutig sein. DIR ist der
	Einhängepunkt der Festplatte im Dateisystem.

	Sie können den Einhängepunkt nach Bedarf ändern, solange Sie
	den NAME unverändert lassen.

	Sie sollten eine Option für jede Datenfestplatte im Array verwenden.

	Sie können eine Festplatte später umbenennen, indem Sie den NAME
	direkt in der Konfigurationsdatei ändern und dann einen `sync`-Befehl
	ausführen. Im Falle einer Umbenennung erfolgt die Zuordnung
	mithilfe der gespeicherten UUID der Festplatten.

  nohidden
	Schließt alle versteckten Dateien und Verzeichnisse aus.
	Unter Unix sind versteckte Dateien solche, die mit `.` beginnen.
	Unter Windows sind es solche mit dem versteckten Attribut.

  exclude/include PATTERN
	Definiert die Datei- oder Verzeichnismuster, die im `sync`-Vorgang
	ausgeschlossen oder eingeschlossen werden sollen.
	Alle Muster werden in der angegebenen Reihenfolge verarbeitet.

	Wenn das erste übereinstimmende Muster ein `exclude`-Muster ist,
	wird die Datei ausgeschlossen. Wenn es ein `include`-Muster ist,
	wird die Datei eingeschlossen. Wenn kein Muster übereinstimmt,
	wird die Datei ausgeschlossen, wenn das zuletzt angegebene Muster
	ein `include` ist, oder eingeschlossen, wenn das zuletzt angegebene
	Muster ein `exclude` ist.

	Weitere Details zu Musterspezifikationen finden Sie im Abschnitt
	PATTERN.

	Diese Option kann mehrmals verwendet werden.

  blocksize SIZE_IN_KIBIBYTES
	Definiert die grundlegende Blockgröße in Kibibytes für die Parität.
	Ein Kibibyte entspricht 1024 Bytes.

	Die Standard-Blockgröße ist 256, was für die meisten Fälle
	ausreichen sollte.

	WARNUNG! Diese Option ist nur für Experten gedacht, und es wird
	dringend empfohlen, diesen Wert nicht zu ändern. Um diesen Wert
	zukünftig zu ändern, müssen Sie die gesamte Parität neu erstellen!

	Ein Grund für die Verwendung einer anderen Blockgröße ist, wenn Sie
	viele kleine Dateien in der Größenordnung von Millionen haben.

	Für jede Datei, auch wenn sie nur wenige Bytes groß ist, wird ein
	ganzer Block Parität zugewiesen, und bei vielen Dateien kann dies
	zu erheblichem ungenutztem Paritätsplatz führen. Wenn Sie die
	Paritätsfestplatte vollständig füllen, dürfen Sie keine weiteren
	Dateien zu den Datenfestplatten hinzufügen.
	Der verschwendete Paritätsplatz akkumuliert sich jedoch nicht über
	die Datenfestplatten hinweg. Verschwendeter Platz, der durch eine
	hohe Anzahl von Dateien auf einer Datenfestplatte entsteht, begrenzt
	nur die Datenmenge auf dieser Datenfestplatte, nicht auf anderen.

	Als Annäherung können Sie davon ausgehen, dass die Hälfte der
	Blockgröße für jede Datei verschwendet wird. Mit beispielsweise
	100.000 Dateien und einer Blockgröße von 256 KiB verschwenden
	Sie 12,8 GB Parität, was zu 12,8 GB weniger verfügbarem Speicherplatz
	auf der Datenfestplatte führen kann.

	Sie können die Menge des verschwendeten Platzes auf jeder Festplatte
	mithilfe von `status` überprüfen. Dies ist die Menge an Platz, die Sie
	auf den Datenfestplatten freilassen oder für Dateien verwenden müssen,
	die nicht im Array enthalten sind. Wenn dieser Wert negativ ist,
	bedeutet dies, dass Sie kurz davor sind, die Parität zu füllen,
	und er stellt den Platz dar, den Sie noch verschwenden können.

	Um dieses Problem zu vermeiden, können Sie eine größere Partition
	für die Parität verwenden. Wenn die Paritätspartition beispielsweise
	12,8 GB größer ist als die Datenfestplatten, haben Sie genügend
	zusätzlichen Platz, um bis zu 100.000 Dateien auf jeder Datenfestplatte
	ohne verschwendeten Platz zu verarbeiten.

	Ein Trick, um eine größere Paritätspartition unter Linux zu erhalten,
	ist die Formatierung mit dem Befehl:

		:mkfs.ext4 -m 0 -T largefile4 DEVICE

	Dies führt zu etwa 1,5% zusätzlichem Speicherplatz, ungefähr 60 GB
	für eine 4 TB Festplatte, was etwa 460.000 Dateien auf jeder
	Datenfestplatte ohne verschwendeten Platz ermöglicht.

  hashsize SIZE_IN_BYTES
	Definiert die Hash-Größe in Bytes für die gespeicherten Blöcke.

	Die Standard-Hash-Größe beträgt 16 Bytes (128 Bit), was für die
	meisten Fälle ausreichen sollte.

	WARNUNG! Diese Option ist nur für Experten gedacht, und es wird
	dringend empfohlen, diesen Wert nicht zu ändern. Um diesen Wert
	zukünftig zu ändern, müssen Sie die gesamte Parität neu erstellen!

	Ein Grund für die Verwendung einer anderen Hash-Größe ist, wenn Ihr
	System über begrenzten Speicher verfügt. Als Faustregel benötigt
	SnapRAID typischerweise 1 GiB RAM für jede 16 TB Daten im Array.

	Genauer gesagt benötigt SnapRAID zum Speichern der Hashes der Daten
	ungefähr TS*(1+HS)/BS Bytes RAM,
	wobei TS die Gesamtgröße in Bytes Ihres Festplatten-Arrays ist,
	BS die Blockgröße in Bytes und HS die Hash-Größe in Bytes.

	Mit beispielsweise 8 Festplatten à 4 TB, einer Blockgröße von 256 KiB
	(1 KiB = 1024 Bytes) und einer Hash-Größe von 16 erhalten Sie:

	:RAM = (8 * 4 * 10^12) * (1+16) / (256 * 2^10) = 1.93 GiB

	Bei einem Wechsel zu einer Hash-Größe von 8 erhalten Sie:

	:RAM = (8 * 4 * 10^12) * (1+8) / (256 * 2^10) = 1.02 GiB

	Bei einem Wechsel zu einer Blockgröße von 512 erhalten Sie:

	:RAM = (8 * 4 * 10^12) * (1+16) / (512 * 2^10) = 0.96 GiB

	Bei einem Wechsel zu einer Hash-Größe von 8 und einer Blockgröße
	von 512 erhalten Sie:

	:RAM = (8 * 4 * 10^12) * (1+8) / (512 * 2^10) = 0.51 GiB

  autosave SIZE_IN_GIGABYTES
	Speichert den Zustand beim Synchronisieren oder Scrubben automatisch
	nach der angegebenen Menge an verarbeiteten GB.
	Diese Option ist nützlich, um zu vermeiden, dass lange `sync`-Befehle
	von vorne gestartet werden müssen, wenn sie durch einen Maschinenabsturz
	oder ein anderes Ereignis unterbrochen werden.

  temp_limit TEMPERATURE_CELSIUS
	Legt die maximal zulässige Festplattentemperatur in Celsius fest.
	Wenn angegeben, überprüft SnapRAID regelmäßig die Temperatur aller
	Festplatten mithilfe des smartctl-Tools. Die aktuellen Festplattentemperaturen
	werden während des Betriebs von SnapRAID angezeigt. Wenn eine Festplatte
	diesen Grenzwert überschreitet, werden alle Vorgänge gestoppt, und
	die Festplatten werden für die durch die Option `temp_sleep` definierte
	Dauer heruntergefahren (in den Standby-Modus versetzt). Nach der
	Schlafperiode werden die Vorgänge fortgesetzt und möglicherweise
	erneut pausiert, wenn der Temperaturgrenzwert erneut erreicht wird.

	Während des Betriebs analysiert SnapRAID auch die Heizkurve jeder
	Festplatte und schätzt die langfristige stationäre Temperatur, die
	erwartet wird, wenn die Aktivität fortgesetzt wird. Die Schätzung
	wird nur durchgeführt, nachdem die Festplattentemperatur viermal
	angestiegen ist, um sicherzustellen, dass genügend Datenpunkte
	zur Verfügung stehen, um einen zuverlässigen Trend festzustellen.
	Diese vorhergesagte stationäre Temperatur wird in Klammern neben
	dem aktuellen Wert angezeigt und hilft bei der Beurteilung, ob die
	Kühlung des Systems ausreichend ist. Diese geschätzte Temperatur
	dient nur zu Informationszwecken und hat keine Auswirkung auf das
	Verhalten von SnapRAID. Die Aktionen des Programms basieren
	ausschließlich auf den tatsächlich gemessenen Festplattentemperaturen.

	Um diese Analyse durchzuführen, benötigt SnapRAID eine Referenz für
	die Systemtemperatur. Es versucht zunächst, diese von verfügbaren
	Hardwaresensoren zu lesen. Wenn kein Systemsensor zugänglich ist,
	wird die niedrigste zu Beginn des Laufs gemessene Festplattentemperatur
	als Fallback-Referenz verwendet.

	Normalerweise zeigt SnapRAID nur die Temperatur der heißesten
	Festplatte an. Um die Temperatur aller Festplatten anzuzeigen,
	verwenden Sie die Option -A oder --stats.

  temp_sleep TIME_IN_MINUTES
	Legt die Standby-Zeit in Minuten fest, wenn der Temperaturgrenzwert
	erreicht wird. Während dieser Zeit bleiben die Festplatten heruntergefahren.
	Der Standardwert ist 5 Minuten.

  pool DIR
	Definiert das Pooling-Verzeichnis, in dem die virtuelle Ansicht des
	Festplatten-Arrays mithilfe des Befehls `pool` erstellt wird.

	Das Verzeichnis muss bereits existieren.

  share UNC_DIR
	Definiert den Windows UNC-Pfad, der für den Remote-Zugriff auf die
	Festplatten erforderlich ist.

	Wenn diese Option angegeben ist, verwenden die im Pool-Verzeichnis
	erstellten symbolischen Links diesen UNC-Pfad, um auf die Festplatten
	zuzugreifen. Ohne diese Option verwenden die generierten symbolischen
	Links nur lokale Pfade, was die Freigabe des Pool-Verzeichnisses
	über das Netzwerk nicht ermöglicht.

	Die symbolischen Links werden unter Verwendung des angegebenen UNC-Pfads
	gebildet, wobei der in der `data`-Option angegebene Festplattenname
	und schließlich das Dateiverzeichnis und der Name hinzugefügt werden.

	Diese Option ist nur für Windows erforderlich.

  smartctl DISK/PARITY OPTIONS...
	Definiert benutzerdefinierte smartctl-Optionen, um die SMART-Attribute
	für jede Festplatte abzurufen. Dies kann für RAID-Controller und einige
	USB-Festplatten erforderlich sein, die nicht automatisch erkannt
	werden können. Der Platzhalter %s wird durch den Gerätenamen ersetzt,
	ist aber für feste Geräte wie RAID-Controller optional.

	DISK ist derselbe Festplattenname, der in der `data`-Option angegeben
	ist. PARITY ist einer der Paritätsnamen: `parity`, `2-parity`,
	`3-parity`, `4-parity`, `5-parity`, `6-parity` oder `z-parity`.

	In den angegebenen OPTIONS wird die Zeichenfolge `%s` durch den
	Gerätenamen ersetzt. Bei RAID-Controllern ist das Gerät
	wahrscheinlich fest, und Sie müssen `%s` möglicherweise nicht verwenden.

	Weitere mögliche Optionen finden Sie in der smartmontools-Dokumentation:

		:https://www.smartmontools.org/wiki/Supported_RAID-Controllers
		:https://www.smartmontools.org/wiki/Supported_USB-Devices

	Zum Beispiel:

		:smartctl parity -d sat %s

  smartignore DISK/PARITY ATTR [ATTR...]
	Ignoriert das angegebene SMART-Attribut bei der Berechnung der
	Wahrscheinlichkeit eines Festplattenausfalls. Diese Option ist
	nützlich, wenn eine Festplatte ungewöhnliche oder irreführende
	Werte für ein bestimmtes Attribut meldet.

	DISK ist derselbe Festplattenname, der in der `data`-Option angegeben
	ist. PARITY ist einer der Paritätsnamen: `parity`, `2-parity`,
	`3-parity`, `4-parity`, `5-parity`, `6-parity` oder `z-parity`.
	Der Sonderwert * kann verwendet werden, um das Attribut auf allen
	Festplatten zu ignorieren.

	Um beispielsweise das Attribut `Current Pending Sector Count` auf allen
	Festplatten zu ignorieren:

		:smartignore * 197

	Um es nur auf der ersten Paritätsfestplatte zu ignorieren:

		:smartignore parity 197

  Examples
	Ein Beispiel für eine typische Konfiguration für Unix ist:

		:parity /mnt/diskp/snapraid.parity
		:content /mnt/diskp/snapraid.content
		:content /var/snapraid/snapraid.content
		:data d1 /mnt/disk1/
		:data d2 /mnt/disk2/
		:data d3 /mnt/disk3/
		:exclude /lost+found/
		:exclude /tmp/
		:smartctl d1 -d sat %s
		:smartctl d2 -d usbjmicron %s
		:smartctl parity -d areca,1/1 /dev/sg0
		:smartctl 2-parity -d areca,2/1 /dev/sg0

	Ein Beispiel für eine typische Konfiguration für Windows ist:

		:parity E:\snapraid.parity
		:content E:\snapraid.content
		:content C:\snapraid\snapraid.content
		:data d1 G:\array\
		:data d2 H:\array\
		:data d3 I:\array\
		:exclude Thumbs.db
		:exclude \$RECYCLE.BIN
		:exclude \System Volume Information
		:smartctl d1 -d sat %s
		:smartctl d2 -d usbjmicron %s
		:smartctl parity -d areca,1/1 /dev/arcmsr0
		:smartctl 2-parity -d areca,2/1 /dev/arcmsr0

Pattern
	Muster bieten eine flexible Möglichkeit, Dateien für die Einbeziehung oder
	den Ausschluss zu filtern. Durch die Verwendung von Globbing-Zeichen können Sie Regeln definieren,
	die mit bestimmten Dateinamen oder ganzen Verzeichnisstrukturen übereinstimmen, ohne
	jeden Pfad manuell auflisten zu müssen.

	Das Fragezeichen `?` entspricht einem beliebigen einzelnen Zeichen außer dem
	Verzeichnistrennzeichen. Dies macht es nützlich für den Abgleich von Dateinamen mit variablen
	Zeichen, während das Muster auf eine einzelne Verzeichnisebene beschränkt bleibt.

	Der einfache Stern `*` entspricht einer beliebigen Folge von Zeichen, aber wie das
	Fragezeichen überschreitet er niemals Verzeichnisgrenzen. Er stoppt beim
	Schrägstrich, was ihn für den Abgleich innerhalb einer einzelnen Pfadkomponente
	geeignet macht. Dies ist das Standardverhalten von Wildcards, das vom Shell-
	Globbing bekannt ist.

	Der Doppelstern `**` ist mächtiger; er entspricht jeder Folge von
	Zeichen einschließlich Verzeichnistrennzeichen. Dies ermöglicht es Mustern, über
	mehrere Verzeichnisebenen hinweg übereinzustimmen. Wenn `**` direkt in
	ein Muster eingebettet ist, kann es null oder mehr Zeichen einschließlich Schrägstrichen zwischen
	dem umgebenden wörtlichen Text entsprechen.

	Die wichtigste Verwendung von `**` ist in der speziellen Form `/**/`. Dies entspricht
	null oder mehr vollständigen Verzeichnisebenen, was es möglich macht, Dateien
	in jeder Tiefe eines Verzeichnisbaums abzugleichen, ohne die genaue Pfadstruktur zu kennen.
	Zum Beispiel entspricht das Muster `src/**/main.js` dem Pfad `src/main.js` (Überspringen
	von null Verzeichnissen), `src/ui/main.js` (Überspringen eines Verzeichnisses) und
	`src/ui/components/main.js` (Überspringen von zwei Verzeichnissen).

	Zeichenklassen mit eckigen Klammern `[]` entsprechen einem einzelnen Zeichen aus einer
	bestimmten Menge oder einem Bereich. Wie die anderen Einzelzeichenmuster
	entsprechen sie nicht den Verzeichnistrennzeichen. Klassen unterstützen Bereiche und Negierung durch
	ein Ausrufezeichen.

	Der grundlegende Unterschied, den man sich merken sollte, ist, dass `*`, `?` und Zeichenklassen
	alle Verzeichnisgrenzen respektieren und nur innerhalb einer einzelnen Pfadkomponente
	übereinstimmen, während `**` das einzige Muster ist, das über Verzeichnistrennzeichen
	hinweg übereinstimmen kann.

	Es gibt vier verschiedene Arten von Mustern:

	=FILE
		Wählt jede Datei mit dem Namen FILE aus.
		Dieses Muster gilt nur für Dateien, nicht für Verzeichnisse.

	=DIR/
		Wählt jedes Verzeichnis mit dem Namen DIR und alles darin aus.
		Dieses Muster gilt nur für Verzeichnisse, nicht für Dateien.

	=/PATH/FILE
		Wählt den exakt angegebenen Dateipfad aus. Dieses Muster gilt
		nur für Dateien, nicht für Verzeichnisse.

	=/PATH/DIR/
		Wählt den exakt angegebenen Verzeichnispfad und alles darin aus.
		Dieses Muster gilt nur für Verzeichnisse, nicht für Dateien.

	Wenn Sie einen absoluten Pfad angeben, der mit / beginnt, wird er
	auf das Array-Stammverzeichnis angewendet, nicht auf das lokale
	Dateisystem-Stammverzeichnis.

	Unter Windows können Sie den Backslash \ anstelle des Forward-Slash /
	verwenden. Windows-Systemverzeichnisse, Junctions, Einhängepunkte
	(Mount Points) und andere spezielle Windows-Verzeichnisse werden
	als Dateien behandelt, was bedeutet, dass Sie zum Ausschließen
	eine Dateiregel verwenden müssen, nicht eine Verzeichnisregel.

	Wenn der Dateiname ein Zeichen '*', '?', '[', oder ']' enthält,
	müssen Sie es escapen, um zu vermeiden, dass es als Globbing-Zeichen
	interpretiert wird. Unter Unix ist das Escape-Zeichen '\'; unter
	Windows ist es '^'. Wenn sich das Muster in der Befehlszeile befindet,
	müssen Sie das Escape-Zeichen verdoppeln, um zu vermeiden, dass es
	von der Befehlsshell interpretiert wird.

	In der Konfigurationsdatei können Sie verschiedene Strategien zum
	Filtern der zu verarbeitenden Dateien verwenden.
	Der einfachste Ansatz besteht darin, nur `exclude`-Regeln zu verwenden,
	um alle Dateien und Verzeichnisse zu entfernen, die Sie nicht
	verarbeiten möchten. Zum Beispiel:

		:# Schließt jede Datei namens `*.unrecoverable` aus
		:exclude *.unrecoverable
		:# Schließt das Stammverzeichnis `/lost+found` aus
		:exclude /lost+found/
		:# Schließt jedes Unterverzeichnis namens `tmp` aus
		:exclude tmp/

	Der gegenteilige Ansatz besteht darin, nur die Dateien zu definieren,
	die Sie verarbeiten möchten, indem Sie nur `include`-Regeln verwenden.
	Zum Beispiel:

		:# Schließt nur einige Verzeichnisse ein
		:include /movies/
		:include /musics/
		:include /pictures/

	Der letzte Ansatz besteht darin, `exclude`- und `include`-Regeln
	zu mischen. In diesem Fall ist die Reihenfolge der Regeln wichtig.
	Frühere Regeln haben Vorrang vor späteren.
	Zur Vereinfachung können Sie zuerst alle `exclude`-Regeln und
	dann alle `include`-Regeln auflisten. Zum Beispiel:

		:# Schließt jede Datei namens `*.unrecoverable` aus
		:exclude *.unrecoverable
		:# Schließt jedes Unterverzeichnis namens `tmp` aus
		:exclude tmp/
		:# Schließt nur einige Verzeichnisse ein
		:include /movies/
		:include /musics/
		:include /pictures/

	In der Befehlszeile können Sie mit der Option -f nur `include`-Muster
	verwenden. Zum Beispiel:

		:# Überprüft nur die .mp3-Dateien.
		:# Unter Unix Anführungszeichen verwenden, um die Globbing-Erweiterung durch die Shell zu vermeiden.
		:snapraid -f "*.mp3" check

	Unter Unix müssen Sie Globbing-Zeichen in der Befehlszeile in
	Anführungszeichen setzen, um zu verhindern, dass die Shell sie erweitert.

Dateien Ignorieren
	Zusätzlich zu den globalen Regeln in der Konfigurationsdatei können Sie
	`.snapraidignore`-Dateien in jedem Verzeichnis innerhalb des Arrays platzieren,
	um dezentrale Ausschlussregeln zu definieren.

	Regeln, die in `.snapraidignore` definiert sind, werden nach den Regeln in der
	Konfigurationsdatei angewendet. Dies bedeutet, dass sie eine höhere Priorität haben
	und verwendet werden können, um Dateien auszuschließen, die zuvor durch die globale
	Konfiguration eingeschlossen wurden. Effektiv wird eine Datei ausgeschlossen, wenn
	eine lokale Regel zutrifft, unabhängig von den globalen Include-Einstellungen.

	Die Musterlogik in `.snapraidignore` spiegelt die globale Konfiguration wider,
	verankert die Muster jedoch an dem Verzeichnis, in dem sich die Datei befindet:

	=FILE
		Wählt jede Datei namens FILE in diesem Verzeichnis oder darunter aus.
		Dies folgt denselben Globbing-Regeln wie das globale Muster.

	=DIR/
		Wählt jedes Verzeichnis namens DIR und alles darin aus, das sich
		in diesem Verzeichnis oder darunter befindet.

	=/PATH/FILE
		Wählt die exakt angegebene Datei relativ zum Speicherort
		der `.snapraidignore`-Datei aus.

	=/PATH/DIR/
		Wählt das exakt angegebene Verzeichnis und alles darin aus,
		relativ zum Speicherort der `.snapraidignore`-Datei.

	Im Gegensatz zur globalen Konfiguration unterstützen `.snapraidignore`-Dateien nur
	Ausschlussregeln; Sie können keine `include`-Muster oder Negationen (!) verwenden.

	Wenn Sie beispielsweise eine `.snapraidignore` in `/mnt/disk1/projects/` haben:

		:# Schließt NUR /mnt/disk1/projects/output.bin aus
		:/output.bin
		:# Schließt jedes Verzeichnis namens 'build' innerhalb von projects/ aus
		:build/
		:# Schließt jede .tmp-Datei innerhalb von projects/ oder deren Unterordnern aus
		:*.tmp

Content
	SnapRAID speichert die Liste und Prüfsummen Ihrer Dateien in der
	Content-Datei.

	Es ist eine Binärdatei, die alle im Festplatten-Array vorhandenen
	Dateien auflistet, zusammen mit allen Prüfsummen zur Überprüfung
	ihrer Integrität.

	Diese Datei wird von den Befehlen `sync` und `scrub` gelesen und
	geschrieben und von den Befehlen `fix`, `check` und `status` gelesen.

Parity
	SnapRAID speichert die Paritätsinformationen Ihres Arrays in den
	Paritätsdateien.

	Dies sind Binärdateien, die die berechnete Parität aller in der
	`content`-Datei definierten Blöcke enthalten.

	Diese Dateien werden von den Befehlen `sync` und `fix` gelesen
	und geschrieben und nur von den Befehlen `scrub` und `check` gelesen.

Encoding
	SnapRAID ignoriert unter Unix jede Kodierung. Es liest und speichert
	die Dateinamen mit derselben Kodierung, die vom Dateisystem
	verwendet wird.

	Unter Windows werden alle vom Dateisystem gelesenen Namen konvertiert
	und im UTF-8-Format verarbeitet.

	Um Dateinamen korrekt auszugeben, müssen Sie die Windows-Konsole
	mit dem Befehl `chcp 65001` auf den UTF-8-Modus einstellen und
	eine TrueType-Schriftart wie `Lucida Console` als Konsolenschriftart
	verwenden. Dies betrifft nur die gedruckten Dateinamen; wenn Sie
	die Konsolenausgabe in eine Datei umleiten, ist die resultierende
	Datei immer im UTF-8-Format.

Copyright
	Diese Datei ist Copyright (C) 2025 Andrea Mazzoleni

See Also
	snapraid_log(1), snapraidd(1)

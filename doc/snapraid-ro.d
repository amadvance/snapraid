Name{number}
	snapraid - SnapRAID Backup pentru Matrici de Discuri

Sintaxă
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

Descriere
	SnapRAID este un program de backup conceput pentru matrici de discuri, care stochează
	informații de paritate pentru recuperarea datelor în cazul a până la șase
	defecte de disc.

	Destinat în principal centrelor media de acasă cu fișiere mari,
	care se schimbă rar, SnapRAID oferă câteva caracteristici:

	* Puteți utiliza discuri deja pline cu fișiere fără
		nevoia de a le reformat, accesându-le ca de obicei.
	* Toate datele dvs. sunt hash-uite pentru a asigura integritatea datelor și a preveni
		corupția silențioasă.
	* Când numărul de discuri defecte depășește numărul de parități,
		pierderea de date este limitată la discurile afectate; datele de pe
		celelalte discuri rămân accesibile.
	* Dacă ștergeți accidental fișiere de pe un disc, recuperarea este
		posibilă.
	* Discurile pot avea dimensiuni diferite.
	* Puteți adăuga discuri în orice moment.
	* SnapRAID nu vă blochează datele; puteți înceta să-l utilizați
		oricând fără a reforma sau muta date.
	* Pentru a accesa un fișier, este necesar să se rotească doar un singur disc, economisind
		energie și reducând zgomotul.

	Pentru mai multe informații, vă rugăm să vizitați site-ul oficial SnapRAID:

		:https://www.snapraid.it/

Limitări
	SnapRAID este un hibrid între un program RAID și unul de backup, care urmărește să combine
	cele mai bune beneficii ale ambelor. Cu toate acestea, are unele limitări pe care ar trebui
	să le luați în considerare înainte de a-l utiliza.

	Principala limitare este că, dacă un disc se defectează și nu ați făcut recent o sincronizare,
	este posibil să nu puteți recupera complet.
	Mai exact, este posibil să nu puteți recupera până la dimensiunea
	fișierelor modificate sau șterse de la ultima operațiune de sincronizare.
	Acest lucru se întâmplă chiar dacă fișierele modificate sau șterse nu sunt pe
	discul defect. Acesta este motivul pentru care SnapRAID este mai potrivit pentru
	date care se schimbă rar.

	Pe de altă parte, fișierele nou adăugate nu împiedică recuperarea fișierelor
	deja existente. Veți pierde doar fișierele adăugate recent dacă acestea
	se află pe discul defect.

	Alte limitări ale SnapRAID sunt:

	* Cu SnapRAID, aveți în continuare sisteme de fișiere separate pentru fiecare disc.
		Cu RAID, obțineți un singur sistem de fișiere mare.
	* SnapRAID nu face striping de date.
		Cu RAID, obțineți un spor de viteză prin striping.
	* SnapRAID nu suportă recuperarea în timp real.
		Cu RAID, nu trebuie să vă opriți din lucru atunci când un disc se defectează.
	* SnapRAID poate recupera date doar dintr-un număr limitat de defecte de disc.
		Cu un backup, puteți recupera dintr-o defecțiune completă
		a întregii matrici de discuri.
	* Sunt salvate doar numele fișierelor, mărcile temporale, symlink-urile și hardlink-urile.
		Permisiunile, proprietarul și atributele extinse nu sunt salvate.

Noțiuni de Bază
	Pentru a utiliza SnapRAID, trebuie mai întâi să selectați un disc în matricea dvs. de discuri
	pentru a-l dedica informațiilor de `paritate`. Cu un singur disc pentru paritate, veți
	putea recupera dintr-o singură defecțiune de disc, similar cu RAID5.

	Dacă doriți să recuperați din mai multe defecțiuni de disc, similar cu RAID6,
	trebuie să rezervați discuri suplimentare pentru paritate. Fiecare disc de paritate
	suplimentar permite recuperarea dintr-o defecțiune de disc în plus.

	Ca discuri de paritate, trebuie să le alegeți pe cele mai mari din matrice,
	deoarece informațiile de paritate pot crește până la dimensiunea celui mai mare disc de date
	din matrice.

	Aceste discuri vor fi dedicate stocării fișierelor de `paritate`.
	Nu ar trebui să stocați datele dvs. pe ele.

	Apoi, trebuie să definiți discurile de `date` pe care doriți să le protejați
	cu SnapRAID. Protecția este mai eficientă dacă aceste discuri
	conțin date care se schimbă rar. Din acest motiv, este mai bine să
	NU includeți discul C:\ al Windows sau directoarele Unix /home, /var și /tmp.

	Lista de fișiere este salvată în fișierele de `conținut`, de obicei
	stocate pe discurile de date, de paritate sau de boot.
	Acest fișier conține detaliile backup-ului dvs., inclusiv toate
	sumele de control (checksums) pentru a-i verifica integritatea.
	Fișierul de `conținut` este stocat în mai multe copii, iar fiecare copie trebuie
	să fie pe un disc diferit pentru a se asigura că, chiar și în cazul a multiple
	defecte de disc, cel puțin o copie este disponibilă.

	De exemplu, să presupunem că sunteți interesați doar de un singur nivel de paritate
	de protecție și discurile dvs. se află la:

		:/mnt/diskp <- discul selectat pentru paritate
		:/mnt/disk1 <- primul disc de protejat
		:/mnt/disk2 <- al doilea disc de protejat
		:/mnt/disk3 <- al treilea disc de protejat

	Trebuie să creați fișierul de configurare /etc/snapraid.conf cu
	următoarele opțiuni:

		:parity /mnt/diskp/snapraid.parity
		:content /var/snapraid/snapraid.content
		:content /mnt/disk1/snapraid.content
		:content /mnt/disk2/snapraid.content
		:data d1 /mnt/disk1/
		:data d2 /mnt/disk2/
		:data d3 /mnt/disk3/

	Dacă sunteți pe Windows, ar trebui să utilizați formatul de cale Windows, cu litere de unitate
	și backslash-uri în loc de slash-uri.

		:parity E:\snapraid.parity
		:content C:\snapraid\snapraid.content
		:content F:\array\snapraid.content
		:content G:\array\snapraid.content
		:data d1 F:\array\
		:data d2 G:\array\
		:data d3 H:\array\

	Dacă aveți multe discuri și rămâneți fără litere de unitate, puteți monta
	discurile direct în subfoldere. Vedeți:

		:https://www.google.com/search?q=Windows+mount+point

	În acest moment, sunteți gata să rulați comanda `sync` pentru a construi
	informațiile de paritate.

		:snapraid sync

	Acest proces poate dura câteva ore prima dată, în funcție de dimensiunea
	datelor deja prezente pe discuri. Dacă discurile sunt goale,
	procesul este imediat.

	Îl puteți opri oricând apăsând Ctrl+C, iar la următoarea rulare,
	va relua de unde a fost întrerupt.

	Când această comandă se finalizează, datele dvs. sunt ÎN SIGURANȚĂ.

	Acum puteți începe să utilizați matricea după cum doriți și
	să actualizați periodic informațiile de paritate rulând comanda `sync`.

  Scrubbing (Verificare)
	Pentru a verifica periodic datele și paritatea pentru erori, puteți
	rula comanda `scrub`.

		:snapraid scrub

	Această comandă compară datele din matricea dvs. cu hash-ul calculat
	în timpul comenzii `sync` pentru a verifica integritatea.

	Fiecare rulare a comenzii verifică aproximativ 8% din matrice, excluzând datele
	deja verificate în ultimele 10 zile.
	Puteți utiliza opțiunea -p, --plan pentru a specifica o cantitate diferită
	și opțiunea -o, --older-than pentru a specifica o vârstă diferită în zile.
	De exemplu, pentru a verifica 5% din matrice pentru blocuri mai vechi de 20 de zile, utilizați:

		:snapraid -p 5 -o 20 scrub

	Dacă se găsesc erori silențioase sau de intrare/ieșire în timpul procesului,
	blocurile corespunzătoare sunt marcate ca fiind defecte în fișierul de `conținut`
	și listate în comanda `status`.

		:snapraid status

	Pentru a le repara, puteți utiliza comanda `fix`, filtrând pentru blocuri defecte cu
	opțiunea -e, --filter-error:

		:snapraid -e fix

	La următorul `scrub`, erorile vor dispărea din raportul `status`
	dacă sunt într-adevăr reparate. Pentru a face mai rapid, puteți utiliza -p bad pentru a verifica
	doar blocurile marcate ca defecte.

		:snapraid -p bad scrub

	Rularea `scrub` pe o matrice nesincronizată poate raporta erori cauzate de
	fișiere eliminate sau modificate. Aceste erori sunt raportate în ieșirea `scrub`,
	dar blocurile aferente nu sunt marcate ca defecte.

  Pooling (Agregare)
	Notă: Funcționalitatea de pooling descrisă mai jos a fost înlocuită de instrumentul
	mergerfs, care este acum opțiunea recomandată pentru utilizatorii Linux în
	comunitatea SnapRAID. Mergefs oferă o modalitate mai flexibilă și eficientă
	de a agrega mai multe discuri într-un singur punct de montare unificat,
	permițând accesul neîntrerupt la fișiere pe toată matricea dvs. fără a se baza
	pe link-uri simbolice. Se integrează bine cu SnapRAID pentru protecția parității
	și este utilizat în mod obișnuit în configurații precum OpenMediaVault (OMV)
	sau configurații NAS personalizate.

	Pentru a avea toate fișierele din matricea dvs. afișate în același arbore de directoare,
	puteți activa funcționalitatea de `pooling`. Aceasta creează o vizualizare virtuală
	doar în citire a tuturor fișierelor din matricea dvs. folosind link-uri simbolice.

	Puteți configura directorul de `pooling` în fișierul de configurare cu:

		:pool /pool

	sau, dacă sunteți pe Windows, cu:

		:pool C:\pool

	și apoi rulați comanda `pool` pentru a crea sau actualiza vizualizarea virtuală.

		:snapraid pool

	Dacă utilizați o platformă Unix și doriți să partajați acest director
	peste rețea către mașini Windows sau Unix, ar trebui să adăugați
	următoarele opțiuni la /etc/samba/smb.conf:

		:# În secțiunea global a smb.conf
		:unix extensions = no

		:# În secțiunea share a smb.conf
		:[pool]
		:comment = Pool
		:path = /pool
		:read only = yes
		:guest ok = yes
		:wide links = yes
		:follow symlinks = yes

	În Windows, partajarea link-urilor simbolice peste o rețea necesită ca clienții să
	le rezolve de la distanță. Pentru a permite acest lucru, pe lângă partajarea directorului pool,
	trebuie să partajați și toate discurile în mod independent, folosind numele discurilor
	definite în fișierul de configurare ca puncte de partajare. De asemenea, trebuie să specificați
	în opțiunea `share` din fișierul de configurare calea UNC Windows pe care
	clienții la distanță trebuie să o folosească pentru a accesa aceste discuri partajate.

	De exemplu, operând de pe un server numit `darkstar`, puteți utiliza
	opțiunile:

		:data d1 F:\array\
		:data d2 G:\array\
		:data d3 H:\array\
		:pool C:\pool
		:share \\darkstar

	și partajați următoarele directoare peste rețea:

		:\\darkstar\pool -> C:\pool
		:\\darkstar\d1 -> F:\array
		:\\darkstar\d2 -> G:\array
		:\\darkstar\d3 -> H:\array

	pentru a permite clienților la distanță să acceseze toate fișierele la \\darkstar\pool.

	De asemenea, ar putea fi necesar să configurați clienții la distanță pentru a permite accesul la symlink-uri la distanță cu comanda:

		:fsutil behavior set SymlinkEvaluation L2L:1 R2R:1 L2R:1 R2L:1

  Undeleting (Anulare Ștergere)
	SnapRAID funcționează mai mult ca un program de backup decât un sistem RAID și poate
	fi folosit pentru a restaura sau anula ștergerea fișierelor la starea lor anterioară utilizând
	opțiunea -f, --filter:

		:snapraid fix -f FILE

	sau pentru un director:

		:snapraid fix -f DIR/

	De asemenea, îl puteți utiliza pentru a recupera doar fișiere șterse accidental în interiorul
	unui director folosind opțiunea -m, --filter-missing, care restaurează
	doar fișierele lipsă, lăsându-le pe toate celelalte neatinse.

		:snapraid fix -m -f DIR/

	Sau pentru a recupera toate fișierele șterse de pe toate unitățile cu:

		:snapraid fix -m

  Recuperare
	Cel mai rău s-a întâmplat și ați pierdut unul sau mai multe discuri!

	NU INTRAȚI ÎN PANICĂ! Veți putea să le recuperați!

	Primul lucru pe care trebuie să-l faceți este să evitați modificările ulterioare la matricea dvs. de discuri.
	Dezactivați orice conexiuni la distanță la aceasta și orice procese programate, inclusiv
	orice sincronizare sau verificare SnapRAID programată pe timp de noapte.

	Apoi continuați cu următorii pași.

    PASUL 1 -> Reconfigurare
	Aveți nevoie de spațiu pentru a recupera, ideal pe discuri de rezervă
	suplimentare, dar un disc USB extern sau un disc la distanță vor fi suficiente.

	Modificați fișierul de configurare SnapRAID pentru a face ca opțiunea `data` sau `parity`
	a discului defect să indice o locație cu suficient spațiu gol
	pentru a recupera fișierele.

	De exemplu, dacă discul `d1` a eșuat, schimbați de la:

		:data d1 /mnt/disk1/

	la:

		:data d1 /mnt/new_spare_disk/

	Dacă discul de recuperat este un disc de paritate, actualizați opțiunea `parity`
	corespunzătoare.
	Dacă aveți mai multe discuri defecte, actualizați toate opțiunile lor de configurare.

    PASUL 2 -> Reparare (Fix)
	Rulați comanda fix, stocând jurnalul într-un fișier extern cu:

		:snapraid -d NAME -l fix.log fix

	Unde NAME este numele discului, cum ar fi `d1` în exemplul nostru anterior.
	Dacă discul de recuperat este un disc de paritate, utilizați numele `parity`, `2-parity`,
	etc.
	Dacă aveți mai multe discuri defecte, utilizați mai multe opțiuni -d pentru a le specifica pe toate.

	Această comandă va dura mult timp.

	Asigurați-vă că aveți câțiva gigabytes liberi pentru a stoca fișierul fix.log.
	Rulați-l de pe un disc cu suficient spațiu liber.

	Acum ați recuperat tot ce este recuperabil. Dacă unele fișiere sunt parțial
	sau total nerecuperabile, vor fi redenumite adăugând extensia `.unrecoverable`.

	Puteți găsi o listă detaliată a tuturor blocurilor nerecuperabile în fișierul fix.log
	verificând toate liniile care încep cu `unrecoverable:`.

	Dacă nu sunteți mulțumit de recuperare, o puteți reîncerca de câte
	ori doriți.

	De exemplu, dacă ați eliminat fișiere din matrice după ultima
	`sync`, acest lucru poate duce la nerecuperarea unor fișiere.
	În acest caz, puteți reîncerca `fix`-ul utilizând opțiunea -i, --import,
	specificând unde se află acum aceste fișiere pentru a le include din nou în
	procesul de recuperare.

	Dacă sunteți mulțumit de recuperare, puteți continua,
	dar rețineți că după sincronizare, nu mai puteți reîncerca comanda `fix`
	deloc!

    PASUL 3 -> Verificare (Check)
	Ca o verificare prudentă, puteți rula acum o comandă `check` pentru a vă asigura că
	totul este corect pe discul recuperat.

		:snapraid -d NAME -a check

	Unde NAME este numele discului, cum ar fi `d1` în exemplul nostru anterior.

	Opțiunile -d și -a spun SnapRAID să verifice doar discul specificat
	și să ignore toate datele de paritate.

	Această comandă va dura mult timp, dar dacă nu sunteți excesiv de prudent,
	o puteți sări.

    PASUL 4 -> Sincronizare (Sync)
	Rulați comanda `sync` pentru a resincroniza matricea cu noul disc.

		:snapraid sync

	Dacă totul este recuperat, această comandă este imediată.

Comenzi
	SnapRAID oferă câteva comenzi simple care vă permit să:

	* Tipăriți starea matricei -> `status`
	* Controlați discurile -> `smart`, `probe`, `up`, `down`
	* Faceți un backup/snapshot -> `sync`
	* Verificați periodic datele -> `scrub`
	* Restaurați ultimul backup/snapshot -> `fix`.

	Comenzile trebuie scrise cu litere mici.

  status
	Tipărește un rezumat al stării matricei de discuri.

	Include informații despre fragmentarea parității, cât de vechi
	sunt blocurile fără verificare și toate erorile silențioase înregistrate
	întâlnite în timpul verificării (scrubbing).

	Informațiile prezentate se referă la ultima dată când ați
	rulat `sync`. Modificările ulterioare nu sunt luate în considerare.

	Dacă au fost detectate blocuri defecte, numerele lor de bloc sunt listate.
	Pentru a le repara, puteți utiliza comanda `fix -e`.

	De asemenea, arată un grafic care reprezintă ultima dată când fiecare bloc
	a fost verificat (scrubbed) sau sincronizat. Blocurile verificate sunt afișate cu `*`,
	blocurile sincronizate, dar încă neverificate, cu `o`.

	Nimic nu este modificat.

  smart
	Tipărește un raport SMART al tuturor discurilor din sistem.

	Include o estimare a probabilității de eșec în anul următor,
	permițându-vă să planificați înlocuiri de întreținere a discurilor care prezintă
	atribute suspecte.

	Această estimare a probabilității este obținută prin corelarea atributelor SMART
	ale discurilor cu datele Backblaze disponibile la:

		:https://www.backblaze.com/hard-drive-test-data.html

	Dacă SMART raportează că un disc este pe cale să eșueze, `FAIL` sau `PREFAIL` este tipărit
	pentru acel disc, iar SnapRAID returnează o eroare.
	În acest caz, înlocuirea imediată a discului este puternic recomandată.

	Alte șiruri de stare posibile sunt:
		logfail - În trecut, unele atribute au fost mai mici decât
			pragul.
		logerr - Jurnalul de erori al dispozitivului conține erori.
		selferr - Jurnalul de auto-test al dispozitivului conține erori.

	Dacă opțiunea -v, --verbose este specificată, este furnizată o analiză statistică
	mai profundă. Această analiză vă poate ajuta să decideți dacă aveți nevoie de mai multă
	sau mai puțină paritate.

	Această comandă utilizează instrumentul `smartctl` și este echivalentă cu rularea
	`smartctl -a` pe toate dispozitivele.

	Dacă dispozitivele dvs. nu sunt detectate automat corect, puteți specifica
	o comandă personalizată utilizând opțiunea `smartctl` în fișierul de configurare.

	Nimic nu este modificat.

  probe
	Tipărește starea de ALIMENTARE (POWER) a tuturor discurilor din sistem.

	`Standby` înseamnă că discul nu se rotește. `Active` înseamnă
	că discul se rotește.

	Această comandă utilizează instrumentul `smartctl` și este echivalentă cu rularea
	`smartctl -n standby -i` pe toate dispozitivele.

	Dacă dispozitivele dvs. nu sunt detectate automat corect, puteți specifica
	o comandă personalizată utilizând opțiunea `smartctl` în fișierul de configurare.

	Nimic nu este modificat.

  up
	Pornirea tuturor discurilor din matrice.

	Puteți porni doar discuri specifice utilizând opțiunea -d, --filter-disk.

	Pornirea tuturor discurilor în același timp necesită multă energie.
	Asigurați-vă că sursa dvs. de alimentare o poate susține.

	Nimic nu este modificat.

  down
	Oprirea (spin down) tuturor discurilor din matrice.

	Această comandă utilizează instrumentul `smartctl` și este echivalentă cu rularea
	`smartctl -s standby,now` pe toate dispozitivele.

	Puteți opri doar discuri specifice utilizând opțiunea -d, --filter-disk.

	Pentru a opri automat la eroare, puteți utiliza opțiunea -s, --spin-down-on-error
	cu orice altă comandă, care este echivalentă cu rularea manuală a `down`
	atunci când apare o eroare.

	Nimic nu este modificat.

  diff
	Listează toate fișierele modificate de la ultima `sync` care trebuie
	să li se recalculeze datele de paritate.

	Această comandă nu verifică datele fișierului, ci doar marca temporală,
	dimensiunea și inodul fișierului.

	După listarea tuturor fișierelor modificate, este prezentat un rezumat al modificărilor,
	grupate după:
		equal - Fișiere neschimbate față de înainte.
		added - Fișiere adăugate care nu erau prezente înainte.
		removed - Fișiere eliminate.
		updated - Fișiere cu o dimensiune sau marcă temporală diferită, ceea ce înseamnă că au
			fost modificate.
		moved - Fișiere mutate într-un director diferit pe același disc.
			Sunt identificate având același nume, dimensiune, marcă temporală
			și inod, dar un director diferit.
		copied - Fișiere copiate pe același disc sau pe un disc diferit. Rețineți că dacă
			sunt mutate cu adevărat pe un alt disc, vor fi, de asemenea,
			numărate în `removed`.
			Sunt identificate având același nume, dimensiune și
			marcă temporală. Dacă marca temporală sub-secundă este zero,
			întreaga cale trebuie să se potrivească, nu doar numele.
		restored - Fișiere cu un inod diferit, dar nume, dimensiune și marcă temporală care se potrivesc.
			Acestea sunt de obicei fișiere restaurate după ce au fost șterse.

	Dacă este necesară o `sync`, codul de retur al procesului este 2, în loc de
	cel implicit 0. Codul de retur 1 este utilizat pentru o condiție de eroare generică.

	Nimic nu este modificat.

  sync
	Actualizează informațiile de paritate. Toate fișierele modificate
	din matricea de discuri sunt citite și datele de paritate
	corespunzătoare sunt actualizate.

	Puteți opri acest proces în orice moment apăsând Ctrl+C,
	fără a pierde munca deja efectuată.
	La următoarea rulare, procesul `sync` va relua de unde
	a fost întrerupt.

	Dacă se găsesc erori silențioase sau de intrare/ieșire în timpul procesului,
	blocurile corespunzătoare sunt marcate ca defecte.

	Fișierele sunt identificate prin cale și/sau inod și verificate prin
	dimensiune și marcă temporală.
	Dacă dimensiunea sau marca temporală a fișierului diferă, datele de paritate
	sunt recalculate pentru întregul fișier.
	Dacă fișierul este mutat sau redenumit pe același disc, păstrând același
	inod, paritatea nu este recalculată.
	Dacă fișierul este mutat pe un alt disc, paritatea este recalculată,
	dar informațiile hash calculate anterior sunt păstrate.

	Fișierele de `content` și `parity` sunt modificate dacă este necesar.
	Fișierele din matrice NU sunt modificate.

  scrub
	Verifică (scrubs) matricea, căutând erori silențioase sau de intrare/ieșire în discurile
	de date și de paritate.

	Fiecare invocare verifică aproximativ 8% din matrice, excluzând
	datele deja verificate în ultimele 10 zile.
	Acest lucru înseamnă că verificarea o dată pe săptămână asigură că fiecare bit de date este verificat
	cel puțin o dată la trei luni.

	Puteți defini un plan de verificare sau o cantitate diferită utilizând opțiunea -p, --plan,
	care acceptă:
	bad - Verifică blocurile marcate ca defecte.
	new - Verifică blocurile tocmai sincronizate care nu au fost încă verificate.
	full - Verifică totul.
	0-100 - Verifică procentul specificat de blocuri.

	Dacă specificați o valoare procentuală, puteți utiliza și opțiunea -o, --older-than
	pentru a defini cât de vechi ar trebui să fie blocul.
	Cele mai vechi blocuri sunt verificate primele, asigurând o verificare optimă.
	Dacă doriți să verificați doar blocurile tocmai sincronizate care nu au fost încă verificate,
	utilizați opțiunea `-p new`.

	Pentru a obține detalii despre starea verificării, utilizați comanda `status`.

	Pentru orice eroare silențioasă sau de intrare/ieșire găsită, blocurile corespunzătoare
	sunt marcate ca defecte în fișierul de `content`.
	Aceste blocuri defecte sunt listate în `status` și pot fi reparate cu `fix -e`.
	După reparare, la următorul scrub, vor fi reverificate, iar dacă se găsesc
	corectate, marca defectă va fi eliminată.
	Pentru a verifica doar blocurile defecte, puteți utiliza comanda `scrub -p bad`.

	Este recomandat să rulați `scrub` doar pe o matrice sincronizată pentru a evita
	erorile raportate cauzate de date nesincronizate. Aceste erori sunt recunoscute
	ca nefiind erori silențioase, iar blocurile nu sunt marcate ca defecte,
	dar astfel de erori sunt raportate în ieșirea comenzii.

	Fișierul de `content` este modificat pentru a actualiza ora ultimei verificări
	pentru fiecare bloc și pentru a marca blocurile defecte.
	Fișierele de `parity` NU sunt modificate.
	Fișierele din matrice NU sunt modificate.

  fix
	Repară toate fișierele și datele de paritate.

	Toate fișierele și datele de paritate sunt comparate cu starea snapshot-ului
	salvată la ultima `sync`.
	Dacă se găsește o diferență, este readusă la snapshot-ul stocat.

	ATENȚIE! Comanda `fix` nu face diferența între erori și
	modificări intenționate. Ea revine necondiționat la starea fișierului
	de la ultima `sync`.

	Dacă nu este specificată nicio altă opțiune, întreaga matrice este procesată.
	Utilizați opțiunile de filtrare pentru a selecta un subset de fișiere sau discuri de operat.

	Pentru a repara doar blocurile marcate ca defecte în timpul `sync` și `scrub`,
	utilizați opțiunea -e, --filter-error.
	Spre deosebire de alte opțiuni de filtrare, aceasta aplică reparații doar fișierelor care sunt
	neschimbate de la ultima `sync`.

	SnapRAID redenumește toate fișierele care nu pot fi reparate adăugând extensia
	`.unrecoverable`.

	Înainte de reparare, întreaga matrice este scanată pentru a găsi orice fișiere mutate
	de la ultima operațiune `sync`.
	Aceste fișiere sunt identificate prin marca lor temporală, ignorând numele
	și directorul lor și sunt utilizate în procesul de recuperare dacă este necesar.
	Dacă ați mutat unele dintre ele în afara matricei, puteți utiliza opțiunea -i, --import
	pentru a specifica directoare suplimentare de scanat.

	Fișierele sunt identificate doar prin cale, nu prin inod.

	Fișierul de `content` NU este modificat.
	Fișierele de `parity` sunt modificate dacă este necesar.
	Fișierele din matrice sunt modificate dacă este necesar.

  check
	Verifică toate fișierele și datele de paritate.

	Funcționează ca `fix`, dar simulează doar o recuperare și nu sunt scrise modificări
	în matrice.

	Această comandă este destinată în primul rând verificării manuale,
	cum ar fi după un proces de recuperare sau în alte condiții speciale.
	Pentru verificări periodice și programate, utilizați `scrub`.

	Dacă utilizați opțiunea -a, --audit-only, doar datele fișierului
	sunt verificate, iar datele de paritate sunt ignorate pentru o
	rulare mai rapidă.

	Fișierele sunt identificate doar prin cale, nu prin inod.

	Nimic nu este modificat.

  list
	Listează toate fișierele conținute în matrice la momentul
	ultimei `sync`.

	Cu -v sau --verbose, este afișat și timpul sub-secundă.

	Nimic nu este modificat.

  dup
	Listează toate fișierele duplicate. Două fișiere sunt considerate egale dacă hash-urile
	lor se potrivesc. Datele fișierului nu sunt citite; sunt utilizate doar
	hash-urile precalculate.

	Nimic nu este modificat.

  pool
	Creează sau actualizează o vizualizare virtuală a tuturor
	fișierelor din matricea dvs. de discuri în directorul de `pooling`.

	Fișierele nu sunt copiate, ci legate folosind
	link-uri simbolice.

	La actualizare, toate link-urile simbolice existente și subdirectoarele goale
	sunt șterse și înlocuite cu noua
	vizualizare a matricei. Orice alte fișiere obișnuite sunt lăsate pe loc.

	Nimic nu este modificat în afara directorului pool.

  devices
	Tipărește dispozitivele de nivel scăzut utilizate de matrice.

	Această comandă afișează asocierile de dispozitive din matrice
	și este destinată în principal ca interfață de script.

	Primele două coloane sunt ID-ul și calea dispozitivului de nivel scăzut.
	Următoarele două coloane sunt ID-ul și calea dispozitivului de nivel înalt.
	Ultima coloană este numele discului din matrice.

	În majoritatea cazurilor, aveți un dispozitiv de nivel scăzut pentru fiecare disc din
	matrice, dar în unele configurații mai complexe, puteți avea mai multe
	dispozitive de nivel scăzut utilizate de un singur disc din matrice.

	Nimic nu este modificat.

  touch
	Setează o marcă temporală arbitrară sub-secundă pentru toate fișierele
	care o au setată la zero.

	Acest lucru îmbunătățește capacitatea SnapRAID de a recunoaște fișierele mutate
	și copiate, deoarece face ca marca temporală să fie aproape unică,
	reducând posibilele duplicate.

	Mai exact, dacă marca temporală sub-secundă nu este zero,
	un fișier mutat sau copiat este identificat ca atare dacă se potrivește
	cu numele, dimensiunea și marca temporală. Dacă marca temporală sub-secundă
	este zero, este considerat o copie doar dacă calea completă,
	dimensiunea și marca temporală se potrivesc toate.

	Marca temporală cu precizie de secundă nu este modificată,
	deci toate datele și orele fișierelor dvs. vor fi păstrate.

  rehash
	Programează o rehash-uire a întregii matrici.

	Această comandă schimbă tipul de hash utilizat, de obicei la actualizarea
	de la un sistem pe 32 de biți la unul pe 64 de biți, pentru a trece de la
	MurmurHash3 la SpookyHash, care este mai rapid.

	Dacă utilizați deja hash-ul optim, această comandă
	nu face nimic și vă informează că nu este necesară nicio acțiune.

	Rehash-ul nu este efectuat imediat, ci are loc
	progresiv în timpul `sync` și `scrub`.

	Puteți verifica starea rehash-ului utilizând `status`.

	În timpul rehash-ului, SnapRAID își menține funcționalitatea completă,
	cu singura excepție că `dup` nu poate detecta fișierele duplicate
	folosind un hash diferit.

Opțiuni
	SnapRAID oferă următoarele opțiuni:

	-c, --conf CONFIG
		Selectează fișierul de configurare de utilizat. Dacă nu este specificat, în Unix
		utilizează fișierul `/usr/local/etc/snapraid.conf` dacă există,
		altfel `/etc/snapraid.conf`.
		În Windows, utilizează fișierul `snapraid.conf` din același
		director ca `snapraid.exe`.

	-f, --filter PATTERN
		Filtrează fișierele de procesat în `check` și `fix`.
		Sunt procesate doar fișierele care se potrivesc cu modelul specificat.
		Această opțiune poate fi utilizată de mai multe ori.
		Vedeți secțiunea PATTERN pentru mai multe detalii despre
		specificațiile modelului.
		În Unix, asigurați-vă că caracterele globbing sunt citate dacă sunt utilizate.
		Această opțiune poate fi utilizată doar cu `check` și `fix`.
		Nu poate fi utilizată cu `sync` și `scrub`, deoarece acestea
		procesează întotdeauna întreaga matrice.

	-d, --filter-disk NAME
		Filtrează discurile de procesat în `check`, `fix`, `up` și `down`.
		Trebuie să specificați un nume de disc așa cum este definit în fișierul de configurare.
		Puteți specifica și discurile de paritate cu numele: `parity`, `2-parity`,
		`3-parity`, etc., pentru a limita operațiunile la un anumit disc de paritate.
		Dacă combinați mai multe opțiuni --filter, --filter-disk și --filter-missing,
		sunt selectate doar fișierele care se potrivesc cu toate filtrele.
		Această opțiune poate fi utilizată de mai multe ori.
		Această opțiune poate fi utilizată doar cu `check`, `fix`, `up` și `down`.
		Nu poate fi utilizată cu `sync` și `scrub`, deoarece acestea
		procesează întotdeauna întreaga matrice.

	-m, --filter-missing
		Filtrează fișierele de procesat în `check` și `fix`.
		Sunt procesate doar fișierele lipsă sau șterse din matrice.
		Când este utilizată cu `fix`, aceasta acționează ca o comandă de `undelete` (anulare ștergere).
		Dacă combinați mai multe opțiuni --filter, --filter-disk și --filter-missing,
		sunt selectate doar fișierele care se potrivesc cu toate filtrele.
		Această opțiune poate fi utilizată doar cu `check` și `fix`.
		Nu poate fi utilizată cu `sync` și `scrub`, deoarece acestea
		procesează întotdeauna întreaga matrice.

	-e, --filter-error
		Procesează fișierele cu erori în `check` și `fix`.
		Procesează doar fișierele care au blocuri marcate cu erori silențioase
		sau de intrare/ieșire în timpul `sync` și `scrub`, așa cum sunt listate în `status`.
		Această opțiune poate fi utilizată doar cu `check` și `fix`.

	-p, --plan PERC|bad|new|full
		Selectează planul de verificare (scrub). Dacă PERC este o valoare numerică de la 0 la 100,
		este interpretată ca procentul de blocuri de verificat.
		În loc de un procent, puteți specifica un plan:
		`bad` verifică blocurile defecte, `new` verifică blocurile care nu au fost încă verificate,
		iar `full` verifică totul.
		Această opțiune poate fi utilizată doar cu `scrub`.

	-o, --older-than DAYS
		Selectează cea mai veche parte a matricei de procesat în `scrub`.
		DAYS este vârsta minimă în zile pentru ca un bloc să fie verificat;
		valoarea implicită este 10.
		Blocurile marcate ca defecte sunt întotdeauna verificate, indiferent de această opțiune.
		Această opțiune poate fi utilizată doar cu `scrub`.

	-a, --audit-only
		În `check`, verifică hash-ul fișierelor fără
		a verifica datele de paritate.
		Dacă sunteți interesați doar de verificarea datelor fișierului, această
		opțiune poate accelera semnificativ procesul de verificare.
		Această opțiune poate fi utilizată doar cu `check`.

	-h, --pre-hash
		În `sync`, rulează o fază preliminară de hashing a tuturor datelor noi
		pentru o verificare suplimentară înainte de calculul parității.
		De obicei, în `sync`, nu se face hashing preliminar, iar datele noi
		sunt hash-uite chiar înainte de calculul parității, atunci când sunt citite
		pentru prima dată.
		Acest proces are loc atunci când sistemul este sub
		încărcare mare, cu toate discurile rotindu-se și un CPU ocupat.
		Aceasta este o condiție extremă pentru mașină și, dacă are o
		problemă hardware latentă, erorile silențioase pot trece nedetectate
		deoarece datele nu sunt încă hash-uite.
		Pentru a evita acest risc, puteți activa modul `pre-hash` pentru a avea
		toate datele citite de două ori pentru a le asigura integritatea.
		Această opțiune verifică și fișierele mutate în cadrul matricei
		pentru a se asigura că operațiunea de mutare a fost reușită și, dacă este necesar,
		vă permite să rulați o operațiune de reparare (fix) înainte de a continua.
		Această opțiune poate fi utilizată doar cu `sync`.

	-i, --import DIR
		Importă din directorul specificat orice fișiere șterse
		din matrice după ultima `sync`.
		Dacă mai aveți astfel de fișiere, ele pot fi utilizate de `check`
		și `fix` pentru a îmbunătăți procesul de recuperare.
		Fișierele sunt citite, inclusiv în subdirectoare, și sunt
		identificate indiferent de numele lor.
		Această opțiune poate fi utilizată doar cu `check` și `fix`.

	-s, --spin-down-on-error
		La orice eroare, oprește (spin down) toate discurile gestionate înainte de a ieși cu
		un cod de stare diferit de zero. Acest lucru împiedică unitățile să
		rămână active și să se rotească după o operațiune întreruptă,
		ajutând la evitarea acumulării inutile de căldură și a consumului de energie.
		Utilizați această opțiune pentru a vă asigura că discurile sunt oprite în siguranță
		chiar și atunci când o comandă eșuează.

	-w, --bw-limit RATE
		Aplică o limită globală de lățime de bandă pentru toate discurile. RATE este
		numărul de octeți pe secundă. Puteți specifica un multiplicator
		cum ar fi K, M sau G (de exemplu, --bw-limit 1G).

	-A, --stats
		Activează o vizualizare de stare extinsă care arată informații suplimentare.
		Ecranul afișează două grafice:
		Primul grafic arată numărul de dungi (stripes) tamponate pentru fiecare
		disc, împreună cu calea fișierului care este
		accesat în prezent pe acel disc. În mod obișnuit, cel mai lent disc nu va avea
		tampon disponibil, ceea ce determină lățimea de bandă maximă realizabilă.
		Al doilea grafic arată procentul de timp petrecut așteptând
		în ultimele 100 de secunde. Este de așteptat ca cel mai lent disc să
		cauzeze cea mai mare parte a timpului de așteptare, în timp ce alte discuri ar trebui să aibă
		puțin sau deloc timp de așteptare, deoarece își pot utiliza dungile tamponate.
		Acest grafic arată și timpul petrecut așteptând calculele hash
		și calculele RAID.
		Toate calculele rulează în paralel cu operațiunile de disc.
		Prin urmare, atâta timp cât există timp de așteptare măsurabil pentru
		cel puțin un disc, indică faptul că CPU-ul este suficient de rapid pentru a
		ține pasul cu sarcina de lucru.

	-Z, --force-zero
		Forțează operațiunea nesigură de sincronizare a unui fișier cu dimensiunea zero
		care era anterior non-zero.
		Dacă SnapRAID detectează o astfel de condiție, se oprește din a continua
		dacă nu specificați această opțiune.
		Acest lucru vă permite să detectați cu ușurință când, după o cădere de sistem,
		unele fișiere accesate au fost trunchiate.
		Aceasta este o condiție posibilă în Linux cu sistemele de fișiere ext3/ext4.
		Această opțiune poate fi utilizată doar cu `sync`.

	-E, --force-empty
		Forțează operațiunea nesigură de sincronizare a unui disc cu toate
		fișierele originale lipsă.
		Dacă SnapRAID detectează că toate fișierele prezente inițial
		pe disc lipsesc sau au fost rescrise, se oprește din a continua
		dacă nu specificați această opțiune.
		Acest lucru vă permite să detectați cu ușurință când un sistem de fișiere de date nu este
		montat.
		Este permis, totuși, să aveți o singură schimbare de UUID cu
		paritate simplă și mai multe cu paritate multiplă, deoarece acesta este
		cazul normal la înlocuirea discurilor după o recuperare.
		Această opțiune poate fi utilizată doar cu `sync`, `check` sau
		`fix`.

	-U, --force-uuid
		Forțează operațiunea nesigură de sincronizare, verificare și reparare
		cu discuri care și-au schimbat UUID-ul.
		Dacă SnapRAID detectează că unele discuri și-au schimbat UUID-ul,
		se oprește din a continua dacă nu specificați această opțiune.
		Acest lucru vă permite să detectați când discurile dvs. sunt montate la
		punctele de montare greșite.
		Este permis, totuși, să aveți o singură schimbare de UUID cu
		paritate simplă și mai multe cu paritate multiplă, deoarece acesta este
		cazul normal la înlocuirea discurilor după o recuperare.
		Această opțiune poate fi utilizată doar cu `sync`, `check` sau
		`fix`.

	-D, --force-device
		Forțează operațiunea nesigură de reparare cu discuri inaccesibile
		sau cu discuri pe același dispozitiv fizic.
		De exemplu, dacă ați pierdut două discuri de date și aveți un disc de rezervă pentru a recupera
		doar primul, puteți ignora al doilea disc inaccesibil.
		Sau, dacă doriți să recuperați un disc în spațiul liber rămas pe un
		disc deja utilizat, partajând același dispozitiv fizic.
		Această opțiune poate fi utilizată doar cu `fix`.

	-N, --force-nocopy
		În `sync`, `check` și `fix`, dezactivează euristica de detectare a copiei.
		Fără această opțiune, SnapRAID presupune că fișierele cu aceleași
		atribute, cum ar fi numele, dimensiunea și marca temporală, sunt copii cu
		aceleași date.
		Acest lucru permite identificarea fișierelor copiate sau mutate de pe un disc
		pe altul și refolosirea informațiilor hash deja calculate
		pentru a detecta erori silențioase sau pentru a recupera fișiere lipsă.
		În unele cazuri rare, acest comportament poate duce la rezultate fals pozitive
		sau la un proces lent din cauza multor verificări hash, iar această
		opțiune vă permite să rezolvați astfel de probleme.
		Această opțiune poate fi utilizată doar cu `sync`, `check` și `fix`.

	-F, --force-full
		În `sync`, forțează o recalculare completă a parității.
		Această opțiune poate fi utilizată atunci când adăugați un nou nivel de paritate sau dacă
		ați revenit la un fișier de conținut vechi utilizând date de paritate mai recente.
		În loc să recreați paritatea de la zero, aceasta vă permite
		să reutilizați hash-urile prezente în fișierul de conținut pentru a valida datele
		și a menține protecția datelor în timpul procesului `sync` utilizând
		datele de paritate existente.
		Această opțiune poate fi utilizată doar cu `sync`.

	-R, --force-realloc
		În `sync`, forțează o realocare completă a fișierelor și o reconstruire a parității.
		Această opțiune poate fi utilizată pentru a realoca complet toate fișierele,
		eliminând fragmentarea, în timp ce reutilizați hash-urile prezente în fișierul de conținut
		pentru a valida datele.
		Această opțiune poate fi utilizată doar cu `sync`.
		ATENȚIE! Această opțiune este doar pentru experți și este puternic
		recomandat să nu o utilizați.
		NU aveți protecție a datelor în timpul operațiunii `sync`.

	-l, --log FILE
		Scrie un jurnal detaliat în fișierul specificat.
		Dacă această opțiune nu este specificată, erorile neașteptate sunt tipărite
		pe ecran, putând rezulta o ieșire excesivă în caz de
		multe erori. Când -l, --log este specificat, doar
		erorile fatale care fac SnapRAID să se oprească sunt tipărite
		pe ecran.
		Dacă calea începe cu `>>`, fișierul este deschis
		în modul de adăugare. Aparițiile de `%D` și `%T` în nume sunt
		înlocuite cu data și ora în formatul YYYYMMDD și
		HHMMSS. În fișierele batch Windows, trebuie să dublați
		caracterul `%`, de exemplu, result-%%D.log. Pentru a utiliza `>>`, trebuie
		să închideți numele între ghilimele, de exemplu, `">>result.log"`.
		Pentru a scoate jurnalul la ieșirea standard sau la eroarea standard,
		puteți utiliza `">&1"` și `">&2"`, respectiv.
		Vedeți fișierul snapraid_log.txt sau pagina de manual pentru descrieri ale etichetelor jurnalului.

	-L, --error-limit NUMBER
		Setează o nouă limită de erori înainte de a opri execuția.
		În mod implicit, SnapRAID se oprește dacă întâlnește mai mult de 100
		de erori de intrare/ieșire, indicând că un disc probabil eșuează.
		Această opțiune afectează `sync` și `scrub`, cărora li se permite
		să continue după primul set de erori de disc pentru a încerca
		să își finalizeze operațiunile.
		Cu toate acestea, `check` și `fix` se opresc întotdeauna la prima eroare.

	-S, --start BLKSTART
		Începe procesarea de la numărul de bloc
		specificat. Acest lucru poate fi util pentru a reîncerca verificarea
		sau repararea anumitor blocuri în cazul unui disc deteriorat.
		Această opțiune este în principal pentru recuperarea manuală avansată.

	-B, --count BLKCOUNT
		Procesează doar numărul specificat de blocuri.
		Această opțiune este în principal pentru recuperarea manuală avansată.

	-C, --gen-conf CONTENT
		Generează un fișier de configurare fictiv dintr-un fișier
		de conținut existent.
		Fișierul de configurare este scris la ieșirea standard
		și nu suprascrie unul existent.
		Acest fișier de configurare conține și informațiile
		necesare pentru a reconstrui punctele de montare a discului în cazul în care
		pierdeți întregul sistem.

	-v, --verbose
		Tipărește mai multe informații pe ecran.
		Dacă este specificat o dată, tipărește fișierele excluse
		și statistici suplimentare.
		Această opțiune nu are niciun efect asupra fișierelor jurnal.

	-q, --quiet
		Tipărește mai puține informații pe ecran.
		Dacă este specificat o dată, elimină bara de progres; de două ori,
		operațiunile în curs; de trei ori, mesajele info
		; de patru ori, mesajele de stare.
		Erorile fatale sunt întotdeauna tipărite pe ecran.
		Această opțiune nu are niciun efect asupra fișierelor jurnal.

	-H, --help
		Tipărește un ecran de ajutor scurt.

	-V, --version
		Tipărește versiunea programului.

Configurare
	SnapRAID necesită un fișier de configurare pentru a ști unde se află matricea dvs. de discuri
	și unde să stocheze informațiile de paritate.

	În Unix, utilizează fișierul `/usr/local/etc/snapraid.conf` dacă există,
	altfel `/etc/snapraid.conf`.
	În Windows, utilizează fișierul `snapraid.conf` din același
	director ca `snapraid.exe`.

	Trebuie să conțină următoarele opțiuni (sensibile la majuscule/minuscule):

  parity FILE [,FILE] ...
	Definește fișierele de utilizat pentru a stoca informațiile de paritate.
	Paritatea permite protecția împotriva unei singure defecțiuni de disc,
	similar cu RAID5.

	Puteți specifica mai multe fișiere, care trebuie să fie pe discuri diferite.
	Când un fișier nu mai poate crește, este utilizat următorul.
	Spațiul total disponibil trebuie să fie cel puțin la fel de mare ca cel mai mare disc de date din
	matrice.

	Puteți adăuga fișiere de paritate suplimentare mai târziu, dar nu
	le puteți reordona sau elimina.

	Păstrarea discurilor de paritate rezervate pentru paritate asigură că
	nu devin fragmentate, îmbunătățind performanța.

	În Windows, 256 MB sunt lăsați neutilizați pe fiecare disc pentru a evita
	avertismentul despre discuri pline.

	Această opțiune este obligatorie și poate fi utilizată o singură dată.

  (2,3,4,5,6)-parity FILE [,FILE] ...
	Definește fișierele de utilizat pentru a stoca informații de paritate suplimentare.

	Pentru fiecare nivel de paritate specificat, este activat un nivel suplimentar de protecție:

	* 2-parity activează paritatea dublă RAID6.
	* 3-parity activează paritatea triplă.
	* 4-parity activează paritatea quad (patru).
	* 5-parity activează paritatea penta (cinci).
	* 6-parity activează paritatea hexa (șase).

	Fiecare nivel de paritate necesită prezența tuturor nivelurilor de paritate
	anterioare.

	Aceleași considerații ca pentru opțiunea `parity` se aplică.

	Aceste opțiuni sunt opționale și pot fi utilizate o singură dată.

  z-parity FILE [,FILE] ...
	Definește un fișier și un format alternativ pentru a stoca paritatea triplă.

	Această opțiune este o alternativă la `3-parity`, destinată în primul rând
	CPU-urilor low-end precum ARM sau AMD Phenom, Athlon și Opteron care nu
	suportă setul de instrucțiuni SSSE3. În astfel de cazuri, oferă
	o performanță mai bună.

	Acest format este similar, dar mai rapid decât cel utilizat de ZFS RAIDZ3.
	Ca și ZFS, nu funcționează dincolo de paritatea triplă.

	Când utilizați `3-parity`, veți fi avertizați dacă este recomandat să utilizați
	formatul `z-parity` pentru îmbunătățirea performanței.

	Este posibil să convertiți de la un format la altul ajustând
	fișierul de configurare cu fișierul z-parity sau 3-parity dorit
	și utilizând `fix` pentru a-l recrea.

  content FILE
	Definește fișierul de utilizat pentru a stoca lista și sumele de control (checksums) a tuturor
	fișierelor prezente în matricea dvs. de discuri.

	Poate fi plasat pe un disc utilizat pentru date, paritate sau
	orice alt disc disponibil.
	Dacă utilizați un disc de date, acest fișier este automat exclus
	din procesul `sync`.

	Această opțiune este obligatorie și poate fi utilizată de mai multe ori pentru a salva
	mai multe copii ale aceluiași fișier.

	Trebuie să stocați cel puțin o copie pentru fiecare disc de paritate utilizat
	plus unul. Utilizarea de copii suplimentare nu dăunează.

  data NAME DIR
	Definește numele și punctul de montare al discurilor de date din
	matrice. NAME este utilizat pentru a identifica discul și trebuie
	să fie unic. DIR este punctul de montare al discului în
	sistemul de fișiere.

	Puteți schimba punctul de montare după cum este necesar, atâta timp cât
	păstrați NAME fix.

	Ar trebui să utilizați o opțiune pentru fiecare disc de date din matrice.

	Puteți redenumi un disc mai târziu schimbând NAME direct
	în fișierul de configurare și apoi rulând o comandă `sync`.
	În cazul redenumirii, asocierea se face utilizând UUID-ul stocat
	al discurilor.

  nohidden
	Exclude toate fișierele și directoarele ascunse.
	În Unix, fișierele ascunse sunt cele care încep cu `.`.
	În Windows, sunt cele cu atributul ascuns.

  exclude/include PATTERN
	Definește modelele de fișiere sau directoare de exclus sau inclus
	în procesul de sincronizare.
	Toate modelele sunt procesate în ordinea specificată.

	Dacă primul model care se potrivește este un `exclude`, fișierul
	este exclus. Dacă este un `include`, fișierul este inclus.
	Dacă niciun model nu se potrivește, fișierul este exclus dacă ultimul model
	specificat este un `include`, sau inclus dacă ultimul model
	specificat este un `exclude`.

	Vedeți secțiunea PATTERN pentru mai multe detalii despre
	specificațiile modelului.

	Această opțiune poate fi utilizată de mai multe ori.

  blocksize SIZE_IN_KIBIBYTES
	Definește dimensiunea de bază a blocului în kibibytes pentru paritate.
	Un kibibyte este 1024 de octeți.

	Dimensiunea implicită a blocului este 256, care ar trebui să funcționeze pentru majoritatea cazurilor.

	ATENȚIE! Această opțiune este doar pentru experți și este puternic
	recomandat să nu schimbați această valoare. Pentru a schimba această valoare în
	viitor, va trebui să recreați întreaga paritate!

	Un motiv pentru a utiliza o dimensiune de bloc diferită este dacă aveți multe fișiere
	mici, de ordinul milioanelor.

	Pentru fiecare fișier, chiar dacă are doar câțiva octeți, este alocat un bloc întreg de paritate,
	iar cu multe fișiere, acest lucru poate duce la un spațiu de paritate neutilizat semnificativ.
	Când umpleți complet discul de paritate, nu vi se
	permite să adăugați mai multe fișiere pe discurile de date.
	Cu toate acestea, paritatea irosită nu se acumulează pe discurile de date. Spațiul irosit
	rezultat dintr-un număr mare de fișiere pe un disc de date limitează doar
	cantitatea de date de pe acel disc de date, nu pe celelalte.

	Ca o aproximare, puteți presupune că jumătate din dimensiunea blocului este
	irosită pentru fiecare fișier. De exemplu, cu 100.000 de fișiere și o dimensiune de bloc
	de 256 KiB, veți irosi 12,8 GB de paritate, ceea ce poate duce
	la 12,8 GB mai puțin spațiu disponibil pe discul de date.

	Puteți verifica cantitatea de spațiu irosit pe fiecare disc utilizând `status`.
	Aceasta este cantitatea de spațiu pe care trebuie să o lăsați liberă pe discurile de date
	sau să o utilizați pentru fișiere care nu sunt incluse în matrice.
	Dacă această valoare este negativă, înseamnă că sunteți aproape de a umple
	paritatea și reprezintă spațiul pe care îl puteți irosi încă.

	Pentru a evita această problemă, puteți utiliza o partiție mai mare pentru paritate.
	De exemplu, dacă partiția de paritate este cu 12,8 GB mai mare decât discurile de date,
	aveți suficient spațiu suplimentar pentru a gestiona până la 100.000
	de fișiere pe fiecare disc de date fără niciun spațiu irosit.

	Un truc pentru a obține o partiție de paritate mai mare în Linux este să o formatați
	cu comanda:

		:mkfs.ext4 -m 0 -T largefile4 DEVICE

	Acest lucru duce la aproximativ 1,5% spațiu suplimentar, aproximativ 60 GB pentru
	un disc de 4 TB, ceea ce permite aproximativ 460.000 de fișiere pe fiecare disc de date fără
	niciun spațiu irosit.

  hashsize SIZE_IN_BYTES
	Definește dimensiunea hash-ului în octeți pentru blocurile salvate.

	Dimensiunea implicită a hash-ului este de 16 octeți (128 de biți), care ar trebui să funcționeze
	pentru majoritatea cazurilor.

	ATENȚIE! Această opțiune este doar pentru experți și este puternic
	recomandat să nu schimbați această valoare. Pentru a schimba această valoare în
	viitor, va trebui să recreați întreaga paritate!

	Un motiv pentru a utiliza o dimensiune de hash diferită este dacă sistemul dvs. are
	memorie limitată. Ca regulă generală, SnapRAID necesită de obicei
	1 GiB de RAM pentru fiecare 16 TB de date din matrice.

	Mai exact, pentru a stoca hash-urile datelor, SnapRAID necesită
	aproximativ TS*(1+HS)/BS octeți de RAM,
	unde TS este dimensiunea totală în octeți a matricei dvs. de discuri, BS este
	dimensiunea blocului în octeți, iar HS este dimensiunea hash-ului în octeți.

	De exemplu, cu 8 discuri de 4 TB, o dimensiune de bloc de 256 KiB
	(1 KiB = 1024 octeți) și o dimensiune de hash de 16, obțineți:

	:RAM = (8 * 4 * 10^12) * (1+16) / (256 * 2^10) = 1,93 GiB

	Trecând la o dimensiune de hash de 8, obțineți:

	:RAM = (8 * 4 * 10^12) * (1+8) / (256 * 2^10) = 1,02 GiB

	Trecând la o dimensiune de bloc de 512, obțineți:

	:RAM = (8 * 4 * 10^12) * (1+16) / (512 * 2^10) = 0,96 GiB

	Trecând la ambele, o dimensiune de hash de 8 și o dimensiune de bloc de 512, obțineți:

	:RAM = (8 * 4 * 10^12) * (1+8) / (512 * 2^10) = 0,51 GiB

  autosave SIZE_IN_GIGABYTES
	Salvează automat starea la sincronizare sau verificare după
	cantitatea specificată de GB procesați.
	Această opțiune este utilă pentru a evita repornirea comenzilor `sync` lungi
	de la zero dacă sunt întrerupte de o cădere a mașinii sau de orice alt eveniment.

  temp_limit TEMPERATURE_CELSIUS
	Setează temperatura maximă permisă a discului în Celsius. Când este specificată,
	SnapRAID verifică periodic temperatura tuturor discurilor utilizând
	instrumentul smartctl. Temperaturile curente ale discurilor sunt afișate în timp ce
	SnapRAID funcționează. Dacă un disc depășește această limită, toate operațiunile
	se opresc, iar discurile sunt oprite (puse în standby) pentru durata
	definită de opțiunea `temp_sleep`. După perioada de așteptare, operațiunile
	reiau, putând întrerupe din nou dacă limita de temperatură este atinsă
	încă o dată.

	În timpul funcționării, SnapRAID analizează și curba de încălzire a fiecărui
	disc și estimează temperatura constantă pe termen lung pe care se așteaptă să o
	atingă dacă activitatea continuă. Estimarea este efectuată numai după
	ce temperatura discului a crescut de patru ori, asigurându-se că sunt disponibile
	suficiente puncte de date pentru a stabili o tendință fiabilă.
	Această temperatură constantă prezisă este afișată între paranteze lângă
	valoarea curentă și ajută la evaluarea dacă răcirea sistemului este
	adecvată. Această temperatură estimată este doar în scop informativ
	și nu are niciun efect asupra comportamentului SnapRAID. Acțiunile programului
	se bazează exclusiv pe temperaturile reale măsurate ale discurilor.

	Pentru a efectua această analiză, SnapRAID are nevoie de o referință pentru
	temperatura sistemului. Încearcă mai întâi să o citească de la senzorii hardware
	disponibili. Dacă nu poate fi accesat niciun senzor de sistem, utilizează
	cea mai scăzută temperatură a discului măsurată la începutul rulării ca referință de rezervă.

	În mod normal, SnapRAID arată doar temperatura celui mai fierbinte disc.
	Pentru a afișa temperatura tuturor discurilor, utilizați opțiunea -A sau --stats.

  temp_sleep TIME_IN_MINUTES
	Setează timpul de așteptare (standby), în minute, când limita de temperatură este
	atinsă. În această perioadă, discurile rămân oprite. Valoarea implicită
	este 5 minute.

  pool DIR
	Definește directorul de pooling unde este creată vizualizarea virtuală a matricei
	de discuri utilizând comanda `pool`.

	Directorul trebuie să existe deja.

  share UNC_DIR
	Definește calea UNC Windows necesară pentru a accesa discurile de la distanță.

	Dacă această opțiune este specificată, link-urile simbolice create în directorul pool
	utilizează această cale UNC pentru a accesa discurile.
	Fără această opțiune, link-urile simbolice generate utilizează doar căi locale,
	ceea ce nu permite partajarea directorului pool peste rețea.

	Link-urile simbolice sunt formate utilizând calea UNC specificată, adăugând
	numele discului specificat în opțiunea `data` și, în final, adăugând
	directorul și numele fișierului.

	Această opțiune este necesară doar pentru Windows.

  smartctl DISK/PARITY OPTIONS...
	Definește opțiuni smartctl personalizate pentru a obține atributele SMART pentru
	fiecare disc. Acest lucru poate fi necesar pentru controlerele RAID și unele discuri USB
	care nu pot fi detectate automat. Substituția %s este înlocuită de
	numele dispozitivului, dar este opțională pentru dispozitivele fixe, cum ar fi controlerele RAID.

	DISK este același nume de disc specificat în opțiunea `data`.
	PARITY este unul dintre numele de paritate: `parity`, `2-parity`, `3-parity`,
	`4-parity`, `5-parity`, `6-parity` sau `z-parity`.

	În OPȚIUNILE specificate, șirul `%s` este înlocuit de
	numele dispozitivului. Pentru controlerele RAID, dispozitivul este
	probabil fix și este posibil să nu fie nevoie să utilizați `%s`.

	Consultați documentația smartmontools pentru opțiuni posibile:

		:https://www.smartmontools.org/wiki/Supported_RAID-Controllers
		:https://www.smartmontools.org/wiki/Supported_USB-Devices

	De exemplu:

		:smartctl parity -d sat %s

  smartignore DISK/PARITY ATTR [ATTR...]
	Ignoră atributul SMART specificat la calcularea probabilității
	de eșec a discului. Această opțiune este utilă dacă un disc raportează valori neobișnuite sau
	înșelătoare pentru un anumit atribut.

	DISK este același nume de disc specificat în opțiunea `data`.
	PARITY este unul dintre numele de paritate: `parity`, `2-parity`, `3-parity`,
	`4-parity`, `5-parity`, `6-parity` sau `z-parity`.
	Valoarea specială * poate fi utilizată pentru a ignora atributul pe toate discurile.

	De exemplu, pentru a ignora atributul `Current Pending Sector Count` pe
	toate discurile:

		:smartignore * 197

	Pentru a-l ignora doar pe primul disc de paritate:

		:smartignore parity 197

  Exemple
	Un exemplu de configurare tipică pentru Unix este:

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

	Un exemplu de configurare tipică pentru Windows este:

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

Model (Pattern)
	Modelele oferă o modalitate flexibilă de a filtra fișierele pentru includere sau
	excludere. Folosind caractere de tip globbing, puteți defini reguli care să
	se potrivească cu nume de fișiere specifice sau cu structuri întregi de directoare fără
	a lista manual fiecare cale.

	Semnul întrebării `?` se potrivește cu orice caracter unic, cu excepția
	separatorului de directoare. Acest lucru îl face util pentru potrivirea numelor de fișiere cu
	caractere variabile, păstrând în același timp modelul limitat la un singur nivel de director.

	Asteriscul simplu `*` se potrivește cu orice secvență de caractere, dar, la fel ca
	semnul întrebării, nu trece niciodată de limitele directoarelor. Se oprește la
	slash-ul înainte, ceea ce îl face potrivit pentru potrivirea în cadrul unei singure
	componente a căii. Acesta este comportamentul standard al metacaracterelor, familiar
	din shell globbing.

	Asteriscul dublu `**` este mai puternic, se potrivește cu orice secvență de
	caractere, inclusiv separatori de directoare. Acest lucru permite modelelor să se potrivească
	pe mai multe niveluri de directoare. Când `**` apare inserat direct într-un
	model, se poate potrivi cu zero sau mai multe caractere, inclusiv slash-uri între
	textul literal înconjurător.

	Cea mai importantă utilizare a lui `**` este în forma specială `/**/`. Aceasta se potrivește cu
	zero sau mai multe niveluri complete de directoare, făcând posibilă potrivirea fișierelor
	la orice adâncime într-un arbore de directoare fără a cunoaște structura exactă a căii.
	De exemplu, modelul `src/**/main.js` se potrivește cu `src/main.js` (sărind
	peste zero directoare), `src/ui/main.js` (sărind peste un director) și
	`src/ui/components/main.js` (sărind peste două directoare).

	Clasele de caractere care utilizează paranteze pătrate `[]` se potrivesc cu un singur caracter dintr-un
	set sau interval specificat. La fel ca și celelalte modele de un singur caracter, acestea
	nu se potrivesc cu separatorii de directoare. Clasele suportă intervale și negarea folosind
	un semn de exclamare.

	Distincția fundamentală de reținut este că `*`, `?` și clasele de caractere
	respectă toate limitele directoarelor și se potrivesc doar în cadrul unei singure
	componente a căii, în timp ce `**` este singurul model care se poate potrivi peste
	separatorii de directoare.

	Există patru tipuri diferite de modele:

	=FILE
		Selectează orice fișier cu numele FILE.
		Acest model se aplică numai fișierelor, nu și directoarelor.

	=DIR/
		Selectează orice director cu numele DIR și tot ce se află în interior.
		Acest model se aplică numai directoarelor, nu și fișierelor.

	=/PATH/FILE
		Selectează calea exactă a fișierului specificat. Acest model se aplică
		numai fișierelor, nu și directoarelor.

	=/PATH/DIR/
		Selectează calea exactă a directorului specificat și tot ce se află
		în interior. Acest model se aplică numai directoarelor, nu și fișierelor.

	Când specificați o cale absolută care începe cu /, aceasta este aplicată la
	directorul rădăcină al matricei, nu la directorul rădăcină al sistemului de fișiere local.

	În Windows, puteți utiliza backslash-ul \ în loc de slash-ul /.
	Directoarele de sistem Windows, joncțiunile, punctele de montare și alte directoare
	speciale Windows sunt tratate ca fișiere, ceea ce înseamnă că pentru a le exclude,
	trebuie să utilizați o regulă de fișier, nu una de director.

	Dacă numele fișierului conține un caracter `*`, `?`, `[`,
	sau `]`, trebuie să îl escape-ați pentru a evita să fie interpretat ca un
	caracter de globbing. În Unix, caracterul escape este `\`; în Windows, este `^`.
	Când modelul este pe linia de comandă, trebuie să dublați caracterul escape
	pentru a evita ca acesta să fie interpretat de shell-ul de comandă.

	În fișierul de configurare, puteți utiliza diferite strategii pentru a filtra
	fișierele de procesat.
	Abordarea cea mai simplă este să utilizați doar reguli `exclude` pentru a elimina toate
	fișierele și directoarele pe care nu doriți să le procesați. De exemplu:

		:# Exclude orice fișier numit `*.unrecoverable`
		:exclude *.unrecoverable
		:# Exclude directorul rădăcină `/lost+found`
		:exclude /lost+found/
		:# Exclude orice subdirector numit `tmp`
		:exclude tmp/

	Abordarea opusă este să definiți doar fișierele pe care doriți să le procesați, utilizând
	doar reguli `include`. De exemplu:

		:# Include doar unele directoare
		:include /movies/
		:include /musics/
		:include /pictures/

	Abordarea finală este de a amesteca regulile `exclude` și `include`. În acest caz,
	ordinea regulilor este importantă. Regulile anterioare au
	precedență față de cele ulterioare.
	Pentru a simplifica, puteți lista toate regulile `exclude` mai întâi și apoi
	toate regulile `include`. De exemplu:

		:# Exclude orice fișier numit `*.unrecoverable`
		:exclude *.unrecoverable
		:# Exclude orice subdirector numit `tmp`
		:exclude tmp/
		:# Include doar unele directoare
		:include /movies/
		:include /musics/
		:include /pictures/

	Pe linia de comandă, utilizând opțiunea -f, puteți utiliza doar modele `include`.
	De exemplu:

		:# Verifică doar fișierele .mp3.
		:# În Unix, utilizați ghilimele pentru a evita expansiunea globbing de către shell.
		:snapraid -f "*.mp3" check

	În Unix, când utilizați caractere globbing pe linia de comandă, trebuie
	să le citați pentru a împiedica shell-ul să le extindă.

Fișiere de Ignorat (Ignore File)
	Pe lângă regulile globale din fișierul de configurare, puteți plasa fișiere
	`.snapraidignore` în orice director din array pentru a defini reguli de excludere
	descentralizate.

	Regulile definite în `.snapraidignore` sunt aplicate după regulile din fișierul
	de configurare. Acest lucru înseamnă că au o prioritate mai mare și pot fi
	utilizate pentru a exclude fișiere care au fost incluse anterior de configurația
	globală. Efectiv, dacă o regulă locală se potrivește, fișierul este exclus
	indiferent de setările globale de includere.

	Logica modelelor în `.snapraidignore` oglindește configurația globală, dar
	ancorează modelele de directorul în care se află fișierul:

	=FILE
		Selectează orice fișier numit FILE în acest director sau mai jos.
		Acesta urmează aceleași reguli de globbing ca modelul global.

	=DIR/
		Selectează orice director numit DIR și tot ce se află în interior,
		aflat în acest director sau mai jos.

	=/PATH/FILE
		Selectează fișierul exact specificat raportat la locația
		fișierului `.snapraidignore`.

	=/PATH/DIR/
		Selectează directorul exact specificat și tot ce se află în interior,
		raportat la locația fișierului `.snapraidignore`.

	Spre deosebire de configurația globală, fișierele `.snapraidignore` suportă
	doar reguli de excludere; nu puteți utiliza modele de `include` sau negația (!).

	De exemplu, dacă aveți un `.snapraidignore` în `/mnt/disk1/projects/`:

		:# Exclude DOAR /mnt/disk1/projects/output.bin
		:/output.bin
		:# Exclude orice director numit `build` în interiorul projects/
		:build/
		:# Exclude orice fișier .tmp în interiorul projects/ sau subfolderele sale
		:*.tmp

Conținut (Content)
	SnapRAID stochează lista și sumele de control (checksums) ale fișierelor dvs. în fișierul de conținut.

	Este un fișier binar care listează toate fișierele prezente în matricea dvs. de discuri,
	împreună cu toate sumele de control pentru a le verifica integritatea.

	Acest fișier este citit și scris de comenzile `sync` și `scrub` și
	citit de comenzile `fix`, `check` și `status`.

Paritate (Parity)
	SnapRAID stochează informațiile de paritate ale matricei dvs. în fișierele de paritate.

	Acestea sunt fișiere binare care conțin paritatea calculată a tuturor
	blocurilor definite în fișierul de `content`.

	Aceste fișiere sunt citite și scrise de comenzile `sync` și `fix` și
	doar citite de comenzile `scrub` și `check`.

Codare (Encoding)
	SnapRAID în Unix ignoră orice codare. Citește și stochează
	numele fișierelor cu aceeași codare utilizată de sistemul de fișiere.

	În Windows, toate numele citite din sistemul de fișiere sunt convertite și
	procesate în format UTF-8.

	Pentru a avea numele fișierelor tipărite corect, trebuie să setați consola Windows
	în modul UTF-8 cu comanda `chcp 65001` și să utilizați
	un font TrueType precum `Lucida Console` ca font al consolei.
	Acest lucru afectează doar numele de fișiere tipărite; dacă
	redirecționați ieșirea consolei către un fișier, fișierul rezultat este întotdeauna
	în format UTF-8.

Drept de Autor (Copyright)
	Acest fișier este Copyright (C) 2025 Andrea Mazzoleni

Vezi și (See Also)
	snapraid_log(1), snapraidd(1)

Name{number}
	snapraid - SnapRAID Säkerhetskopiering för Disk-arrayer

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

Beskrivning
	SnapRAID är ett säkerhetskopieringsprogram designat för disk-arrayer,
	som lagrar paritetsinformation för dataåterställning vid upp till sex
	diskfel.

	SnapRAID är i första hand avsett för hemmamediabibliotek med stora,
	sällan ändrade filer, och erbjuder flera funktioner:

	* Du kan använda diskar som redan är fyllda med filer utan
		att behöva formatera om dem, och du får åtkomst till dem som vanligt.
	* All din data hashberäknas för att säkerställa dataintegritet och
		förhindra tyst korruption.
	* När antalet felaktiga diskar överskrider paritetsantalet,
		begränsas dataförlusten till de drabbade diskarna; data på
		andra diskar förblir åtkomlig.
	* Om du oavsiktligt raderar filer på en disk är återställning
		möjlig.
	* Diskar kan ha olika storlekar.
	* Du kan lägga till diskar när som helst.
	* SnapRAID låser inte in din data; du kan sluta använda det
		när som helst utan omformatering eller dataflytt.
	* För att få åtkomst till en fil behöver endast en enda disk snurra,
		vilket sparar ström och minskar buller.

	För mer information, besök den officiella SnapRAID-webbplatsen:

		:https://www.snapraid.it/

Begränsningar
	SnapRAID är en hybrid mellan ett RAID- och ett säkerhetskopieringsprogram,
	som syftar till att kombinera de bästa fördelarna med båda. Den har dock
	vissa begränsningar som du bör överväga innan du använder den.

	Huvudbegränsningen är att om en disk misslyckas och du inte nyligen har
	synkat, kanske du inte kan återställa fullständigt.
	Mer specifikt, kanske du inte kan återställa upp till storleken på
	de ändrade eller raderade filerna sedan den senaste synkroniseringsoperationen.
	Detta inträffar även om de ändrade eller raderade filerna inte finns på
	den felaktiga disken. Det är därför SnapRAID passar bättre för
	data som sällan ändras.

	Å andra sidan förhindrar nyligen tillagda filer inte återställning av redan
	befintliga filer. Du kommer bara att förlora de nyligen tillagda filerna om de
	finns på den felaktiga disken.

	Andra SnapRAID-begränsningar är:

	* Med SnapRAID har du fortfarande separata filsystem för varje disk.
		Med RAID får du ett enda stort filsystem.
	* SnapRAID stripas inte data.
		Med RAID får du en hastighetsökning med striping.
	* SnapRAID stöder inte återställning i realtid.
		Med RAID behöver du inte sluta arbeta när en disk misslyckas.
	* SnapRAID kan bara återställa data från ett begränsat antal diskfel.
		Med en säkerhetskopia kan du återställa från ett fullständigt
		fel på hela disk-arrayen.
	* Endast filnamn, tidsstämplar, symboliska länkar och hårda länkar sparas.
		Behörigheter, ägarskap och utökade attribut sparas inte.

Kom igång
	För att använda SnapRAID måste du först välja en disk i din disk-array
	som ska dediceras till `parity`-information. Med en disk för paritet,
	kommer du att kunna återställa från ett enda diskfel, liknande RAID5.

	Om du vill återställa från fler diskfel, liknande RAID6,
	måste du reservera ytterligare diskar för paritet. Varje ytterligare
	paritetsdisk möjliggör återställning från ett diskfel till.

	Som paritetsdiskar måste du välja de största diskarna i arrayen,
	eftersom paritetsinformationen kan växa till storleken av den största
	datadisken i arrayen.

	Dessa diskar kommer att dediceras till att lagra `parity`-filerna.
	Du bör inte lagra din data på dem.

	Sedan måste du definiera de `data`-diskar som du vill skydda
	med SnapRAID. Skyddet är mer effektivt om dessa diskar
	innehåller data som sällan ändras. Av denna anledning är det bäst att
	INTE inkludera Windows C:\-disken eller Unix /home, /var och /tmp
	katalogerna.

	Listan över filer sparas i `content`-filerna, som vanligtvis
	lagras på data-, paritets- eller startdiskarna.
	Denna fil innehåller detaljerna för din säkerhetskopia, inklusive alla
	checksummor för att verifiera dess integritet.
	`content`-filen lagras i flera kopior, och varje kopia måste
	finnas på en annan disk för att säkerställa att, även vid flera
	diskfel, minst en kopia är tillgänglig.

	Anta till exempel att du bara är intresserad av en paritetsnivå
	av skydd, och dina diskar finns på:

		:/mnt/diskp <- vald disk för paritet
		:/mnt/disk1 <- första disken att skydda
		:/mnt/disk2 <- andra disken att skydda
		:/mnt/disk3 <- tredje disken att skydda

	Du måste skapa konfigurationsfilen /etc/snapraid.conf med
	följande alternativ:

		:parity /mnt/diskp/snapraid.parity
		:content /var/snapraid/snapraid.content
		:content /mnt/disk1/snapraid.content
		:content /mnt/disk2/snapraid.content
		:data d1 /mnt/disk1/
		:data d2 /mnt/disk2/
		:data d3 /mnt/disk3/

	Om du använder Windows bör du använda Windows sökvägsformat, med
	enhetsbeteckningar och omvända snedstreck istället för snedstreck.

		:parity E:\snapraid.parity
		:content C:\snapraid\snapraid.content
		:content F:\array\snapraid.content
		:content G:\array\snapraid.content
		:data d1 F:\array\
		:data d2 G:\array\
		:data d3 H:\array\

	Om du har många diskar och får slut på enhetsbeteckningar kan du montera
	diskar direkt i undermappar. Se:

		:https://www.google.com/search?q=Windows+mount+point

	Vid denna punkt är du redo att köra kommandot `sync` för att bygga
	paritetsinformationen.

		:snapraid sync

	Denna process kan ta flera timmar första gången, beroende på storleken
	på datan som redan finns på diskarna. Om diskarna är tomma är
	processen omedelbar.

	Du kan stoppa det när som helst genom att trycka Ctrl+C, och vid nästa körning
	återupptas det där det avbröts.

	När detta kommando är klart är din data SÄKER.

	Nu kan du börja använda din array som du vill och periodiskt
	uppdatera paritetsinformationen genom att köra kommandot `sync`.

  Skrubbning (Scrubbing)
	För att periodiskt kontrollera data och paritet för fel kan du
	köra kommandot `scrub`.

		:snapraid scrub

	Detta kommando jämför data i din array med den hash som beräknades
	under kommandot `sync` för att verifiera integriteten.

	Varje körning av kommandot kontrollerar ungefär 8% av arrayen, exklusive data
	som redan skrubbats under de föregående 10 dagarna.
	Du kan använda alternativet -p, --plan för att specificera en annan mängd
	och alternativet -o, --older-than för att specificera en annan ålder i dagar.
	Till exempel, för att kontrollera 5% av arrayen för block äldre än 20 dagar, använd:

		:snapraid -p 5 -o 20 scrub

	Om tysta eller input/output-fel hittas under processen,
	markeras de motsvarande blocken som dåliga i `content`-filen
	och listas i kommandot `status`.

		:snapraid status

	För att fixa dem kan du använda kommandot `fix`, filtrera efter dåliga block med
	alternativet -e, --filter-error:

		:snapraid -e fix

	Vid nästa `scrub` kommer felen att försvinna från `status`-rapporten
	om de verkligen är fixade. För att göra det snabbare kan du använda -p bad för att skrubba
	endast block markerade som dåliga.

		:snapraid -p bad scrub

	Att köra `scrub` på en osynkad array kan rapportera fel orsakade av
	borttagna eller modifierade filer. Dessa fel rapporteras i `scrub`-
	utdata, men de relaterade blocken markeras inte som dåliga.

  Poolning
	Obs: Poolningsfunktionen som beskrivs nedan har ersatts av verktyget
	mergerfs, som nu är det rekommenderade alternativet för Linux-användare i
	SnapRAID-communityt. Mergefs ger ett mer flexibelt och effektivt
	sätt att poola flera enheter till en enda enhetlig monteringspunkt,
	vilket möjliggör sömlös åtkomst till filer över din array utan att förlita sig
	på symboliska länkar. Det integreras väl med SnapRAID för paritetsskydd
	och används ofta i installationer som OpenMediaVault (OMV)
	eller anpassade NAS-konfigurationer.

	För att visa alla filer i din array i samma katalogträd,
	kan du aktivera funktionen `pooling`. Den skapar en skrivskyddad virtuell
	vy av alla filer i din array med hjälp av symboliska länkar.

	Du kan konfigurera `pooling`-katalogen i konfigurationsfilen med:

		:pool /pool

	eller, om du använder Windows, med:

		:pool C:\pool

	och kör sedan kommandot `pool` för att skapa eller uppdatera den virtuella vyn.

		:snapraid pool

	Om du använder en Unix-plattform och vill dela denna katalog
	över nätverket till antingen Windows- eller Unix-maskiner, bör du lägga till
	följande alternativ till din /etc/samba/smb.conf:

		:# I den globala sektionen av smb.conf
		:unix extensions = no

		:# I delningssektionen av smb.conf
		:[pool]
		:comment = Pool
		:path = /pool
		:read only = yes
		:guest ok = yes
		:wide links = yes
		:follow symlinks = yes

	I Windows kräver delning av symboliska länkar över ett nätverk att klienter
	löser dem på distans. För att aktivera detta måste du, förutom att dela pool-katalogen,
	även dela alla diskar oberoende, med hjälp av de disk-namn
	som definieras i konfigurationsfilen som delningspunkter. Du måste också specificera
	i alternativet `share` i konfigurationsfilen den Windows UNC-sökväg som
	fjärrklienter behöver använda för att få åtkomst till dessa delade diskar.

	Till exempel, om du arbetar från en server som heter `darkstar`, kan du använda
	alternativen:

		:data d1 F:\array\
		:data d2 G:\array\
		:data d3 H:\array\
		:pool C:\pool
		:share \\darkstar

	och dela följande kataloger över nätverket:

		:\\darkstar\pool -> C:\pool
		:\\darkstar\d1 -> F:\array
		:\\darkstar\d2 -> G:\array
		:\\darkstar\d3 -> H:\array

	för att tillåta fjärrklienter att få åtkomst till alla filer på \\darkstar\pool.

	Du kan också behöva konfigurera fjärrklienter för att möjliggöra åtkomst till
	fjärrsymboliska länkar med kommandot:

		:fsutil behavior set SymlinkEvaluation L2L:1 R2R:1 L2R:1 R2L:1

  Ångra radering (Undeleting)
	SnapRAID fungerar mer som ett säkerhetskopieringsprogram än ett RAID-system, och det
	kan användas för att återställa eller ångra radering av filer till deras tidigare tillstånd
	med hjälp av alternativet -f, --filter:

		:snapraid fix -f FIL

	eller för en katalog:

		:snapraid fix -f KATALOG/

	Du kan också använda det för att återställa endast oavsiktligt raderade filer inuti
	en katalog med hjälp av alternativet -m, --filter-missing, som återställer
	endast saknade filer, och lämnar alla andra orörda.

		:snapraid fix -m -f KATALOG/

	Eller för att återställa alla raderade filer på alla enheter med:

		:snapraid fix -m

  Återställning
	Det värsta har hänt, och du har förlorat en eller flera diskar!

	FÅ INTE PANIK! Du kommer att kunna återställa dem!

	Det första du måste göra är att undvika ytterligare ändringar i din disk-array.
	Inaktivera alla fjärranslutningar till den och alla schemalagda processer, inklusive
	alla schemalagda SnapRAID nattliga sync- eller scrub-körningar.

	Fortsätt sedan med följande steg.

    STEG 1 -> Omkonfigurera
	Du behöver lite utrymme för att återställa, helst på ytterligare
	reservdiskar, men en extern USB-disk eller fjärrdisk räcker.

	Ändra SnapRAID-konfigurationsfilen för att få alternativet `data` eller `parity`
	för den felaktiga disken att peka på en plats med tillräckligt tomt
	utrymme för att återställa filerna.

	Om till exempel disk `d1` har misslyckats, ändra från:

		:data d1 /mnt/disk1/

	till:

		:data d1 /mnt/new_spare_disk/

	Om disken som ska återställas är en paritetsdisk, uppdatera det lämpliga `parity`-
	alternativet.
	Om du har flera felaktiga diskar, uppdatera alla deras konfigurationsalternativ.

    STEG 2 -> Fixa
	Kör kommandot fix och lagra loggen i en extern fil med:

		:snapraid -d NAMN -l fix.log fix

	Där NAMN är namnet på disken, till exempel `d1` i vårt tidigare exempel.
	Om disken som ska återställas är en paritetsdisk, använd namnen `parity`, `2-parity`,
	etc.
	Om du har flera felaktiga diskar, använd flera -d-alternativ för att specificera alla
	av dem.

	Detta kommando kommer att ta lång tid.

	Se till att du har några gigabyte ledigt för att lagra fix.log-filen.
	Kör den från en disk med tillräckligt med ledigt utrymme.

	Nu har du återställt allt som är återställningsbart. Om vissa filer är delvis
	eller helt icke-återställningsbara, kommer de att döpas om genom att lägga till tillägget
	`.unrecoverable`.

	Du kan hitta en detaljerad lista över alla icke-återställningsbara block i fix.log-filen
	genom att kontrollera alla rader som börjar med `unrecoverable:`.

	Om du inte är nöjd med återställningen kan du försöka igen hur många
	gånger du vill.

	Om du till exempel har tagit bort filer från arrayen efter den senaste
	`sync`, kan detta leda till att vissa filer inte återställs.
	I detta fall kan du försöka igen `fix` med alternativet -i, --import,
	specificera var dessa filer nu finns för att inkludera dem igen i
	återställningsprocessen.

	Om du är nöjd med återställningen kan du fortsätta,
	men notera att efter synkronisering kan du inte försöka igen kommandot `fix`
	längre!

    STEG 3 -> Kontrollera
	Som en försiktighetsåtgärd kan du nu köra ett `check`-kommando för att säkerställa att
	allt är korrekt på den återställda disken.

		:snapraid -d NAMN -a check

	Där NAMN är namnet på disken, till exempel `d1` i vårt tidigare exempel.

	Alternativen -d och -a talar om för SnapRAID att endast kontrollera den specificerade disken
	och ignorera all paritetsdata.

	Detta kommando kommer att ta lång tid, men om du inte är alltför försiktig,
	kan du hoppa över det.

    STEG 4 -> Synkronisera
	Kör kommandot `sync` för att synkronisera arrayen igen med den nya disken.

		:snapraid sync

	Om allt är återställt är detta kommando omedelbart.

Kommandon
	SnapRAID tillhandahåller några enkla kommandon som låter dig:

	* Skriva ut statusen för arrayen -> `status`
	* Kontrollera diskarna -> `smart`, `probe`, `up`, `down`
	* Göra en säkerhetskopia/ögonblicksbild -> `sync`
	* Periodiskt kontrollera data -> `scrub`
	* Återställa den senaste säkerhetskopian/ögonblicksbilden -> `fix`.

	Kommandon måste skrivas med gemener.

  status
	Skriver ut en sammanfattning av tillståndet för disk-arrayen.

	Det inkluderar information om paritetsfragmentering, hur gamla
	blocken är utan kontroll, och alla registrerade tysta
	fel som påträffats under skrubbning.

	Informationen som presenteras hänvisar till den senaste tidpunkten du
	körde `sync`. Senare modifieringar beaktas inte.

	Om dåliga block upptäcktes, listas deras blocknummer.
	För att fixa dem kan du använda kommandot `fix -e`.

	Det visar också ett diagram som representerar den senaste tidpunkten då varje block
	skrubbades eller synkroniserades. Skrubbade block visas med `*`,
	block synkroniserade men ännu inte skrubbade med `o`.

	Inget ändras.

  smart
	Skriver ut en SMART-rapport över alla diskar i systemet.

	Det inkluderar en uppskattning av sannolikheten för fel under det kommande
	året, vilket gör att du kan planera underhållsersättningar av diskar som visar
	misstänkta attribut.

	Denna sannolikhetsuppskattning erhålls genom att korrelera SMART-attributen
	för diskarna med Backblaze-data som finns tillgänglig på:

		:https://www.backblaze.com/hard-drive-test-data.html

	Om SMART rapporterar att en disk håller på att misslyckas, skrivs `FAIL` eller `PREFAIL` ut
	för den disken, och SnapRAID returnerar med ett fel.
	I detta fall rekommenderas omedelbar ersättning av disken starkt.

	Andra möjliga statussträngar är:
		logfail - Tidigare var vissa attribut lägre än
			tröskelvärdet.
		logerr - Enhetens fellogg innehåller fel.
		selferr - Enhetens självtestlogg innehåller fel.

	Om alternativet -v, --verbose specificeras, tillhandahålls en djupare statistisk analys.
	Denna analys kan hjälpa dig att bestämma om du behöver mer
	eller mindre paritet.

	Detta kommando använder verktyget `smartctl` och är ekvivalent med att köra
	`smartctl -a` på alla enheter.

	Om dina enheter inte upptäcks automatiskt korrekt kan du specificera
	ett anpassat kommando med hjälp av alternativet `smartctl` i konfigurations-
	filen.

	Inget ändras.

  probe
	Skriver ut STRÖM-tillståndet för alla diskar i systemet.

	`Standby` betyder att disken inte snurrar. `Active` betyder
	att disken snurrar.

	Detta kommando använder verktyget `smartctl` och är ekvivalent med att köra
	`smartctl -n standby -i` på alla enheter.

	Om dina enheter inte upptäcks automatiskt korrekt kan du specificera
	ett anpassat kommando med hjälp av alternativet `smartctl` i konfigurations-
	filen.

	Inget ändras.

  up
	Snurrar upp alla diskar i arrayen.

	Du kan snurra upp endast specifika diskar med hjälp av alternativet -d, --filter-disk.

	Att snurra upp alla diskar samtidigt kräver mycket ström.
	Se till att din strömförsörjning kan klara det.

	Inget ändras.

  down
	Snurrar ner alla diskar i arrayen.

	Detta kommando använder verktyget `smartctl` och är ekvivalent med att köra
	`smartctl -s standby,now` på alla enheter.

	Du kan snurra ner endast specifika diskar med hjälp av alternativet -d, --filter-disk
	alternativet.

	För att automatiskt snurra ner vid fel kan du använda alternativet -s, --spin-down-on-error
	med alla andra kommandon, vilket är ekvivalent med att köra `down` manuellt
	när ett fel inträffar.

	Inget ändras.

  diff
	Listar alla filer som modifierats sedan den senaste `sync` och som behöver få
	sin paritetsdata omberäknad.

	Detta kommando kontrollerar inte filens data, utan endast filens tidsstämpel,
	storlek och inode.

	Efter att ha listat alla ändrade filer presenteras en sammanfattning av ändringarna,
	grupperade efter:
		equal - Filer oförändrade från tidigare.
		added - Filer tillagda som inte fanns tidigare.
		removed - Filer borttagna.
		updated - Filer med en annan storlek eller tidsstämpel, vilket innebär att de
			modifierades.
		moved - Filer flyttade till en annan katalog på samma disk.
			De identifieras genom att ha samma namn, storlek, tidsstämpel
			och inode, men en annan katalog.
		copied - Filer kopierade på samma eller en annan disk. Observera att om
			de verkligen flyttas till en annan disk, kommer de också att
			räknas i `removed`.
			De identifieras genom att ha samma namn, storlek och
			tidsstämpel. Om undertidsstämpeln är noll,
			måste hela sökvägen matcha, inte bara namnet.
		restored - Filer med en annan inode men matchande namn, storlek och tidsstämpel.
			Dessa är vanligtvis filer som återställts efter att ha raderats.

	Om en `sync` krävs är processens returkod 2, istället för standard 0.
	Returkoden 1 används för ett generiskt feltillstånd.

	Inget ändras.

  sync
	Uppdaterar paritetsinformationen. Alla modifierade filer
	i disk-arrayen läses, och motsvarande paritets-
	data uppdateras.

	Du kan stoppa denna process när som helst genom att trycka Ctrl+C,
	utan att förlora det arbete som redan utförts.
	Vid nästa körning kommer `sync`-processen att återupptas där
	den avbröts.

	Om tysta eller input/output-fel hittas under processen,
	markeras de motsvarande blocken som dåliga.

	Filer identifieras med sökväg och/eller inode och kontrolleras med
	storlek och tidsstämpel.
	Om filstorleken eller tidsstämpeln skiljer sig åt, beräknas paritetsdata
	om för hela filen.
	Om filen flyttas eller döps om på samma disk, samtidigt som
	samma inode behålls, beräknas inte pariteten om.
	Om filen flyttas till en annan disk, beräknas pariteten om,
	men den tidigare beräknade hash-informationen behålls.

	`content`- och `parity`-filerna modifieras vid behov.
	Filerna i arrayen modifieras INTE.

  scrub
	Skrubbar arrayen och söker efter tysta eller input/output-fel i data-
	och paritetsdiskar.

	Varje anrop kontrollerar ungefär 8% av arrayen, exklusive
	data som redan skrubbats under de senaste 10 dagarna.
	Detta innebär att skrubbning en gång i veckan säkerställer att varje bit data kontrolleras
	minst en gång var tredje månad.

	Du kan definiera en annan skrubbplan eller mängd med hjälp av alternativet -p, --plan
	som accepterar:
	bad - Skrubbar block markerade som dåliga.
	new - Skrubbar nyligen synkroniserade block som ännu inte skrubbats.
	full - Skrubbar allt.
	0-100 - Skrubbar den specificerade procentandelen av block.

	Om du specificerar en procentsats kan du också använda alternativet -o, --older-than
	för att definiera hur gammalt blocket ska vara.
	De äldsta blocken skrubbas först, vilket säkerställer en optimal kontroll.
	Om du bara vill skrubba de nyligen synkroniserade blocken som ännu inte skrubbats,
	använd alternativet `-p new`.

	För att få detaljer om skrubbstatusen, använd kommandot `status`.

	För alla tysta eller input/output-fel som hittas, markeras de motsvarande blocken
	som dåliga i `content`-filen.
	Dessa dåliga block listas i `status` och kan fixas med `fix -e`.
	Efter fixningen, vid nästa skrubb, kommer de att kontrolleras igen, och om de befinns
	korrigerade, tas det dåliga märket bort.
	För att skrubba endast de dåliga blocken kan du använda kommandot `scrub -p bad`.

	Det rekommenderas att köra `scrub` endast på en synkroniserad array för att undvika
	rapporterade fel orsakade av osynkad data. Dessa fel känns igen
	som att de inte är tysta fel, och blocken markeras inte som dåliga,
	men sådana fel rapporteras i kommandots utdata.

	`content`-filen modifieras för att uppdatera tiden för den senaste kontrollen
	för varje block och för att markera dåliga block.
	`parity`-filerna modifieras INTE.
	Filerna i arrayen modifieras INTE.

  fix
	Fixar alla filer och paritetsdata.

	Alla filer och paritetsdata jämförs med ögonblicksbilds-
	tillståndet som sparades i den senaste `sync`.
	Om en skillnad hittas, återställs den till det lagrade ögonblicksbildstillståndet.

	VARNING! Kommandot `fix` skiljer inte mellan fel och
	avsiktliga modifieringar. Det återställer ovillkorligen filtillståndet
	till den senaste `sync`.

	Om inget annat alternativ specificeras, behandlas hela arrayen.
	Använd filteralternativen för att välja en delmängd av filer eller diskar att arbeta med.

	För att fixa endast blocken markerade som dåliga under `sync` och `scrub`,
	använd alternativet -e, --filter-error.
	Till skillnad från andra filteralternativ tillämpar detta fixar endast på filer som är
	oförändrade sedan den senaste `sync`.

	SnapRAID döper om alla filer som inte kan fixas genom att lägga till tillägget
	`.unrecoverable`.

	Innan fixningen skannas hela arrayen för att hitta alla filer som flyttats
	sedan den senaste `sync`-operationen.
	Dessa filer identifieras av deras tidsstämpel, ignorera deras namn
	och katalog, och används i återställningsprocessen vid behov.
	Om du flyttade några av dem utanför arrayen kan du använda alternativet -i, --import
	för att specificera ytterligare kataloger att skanna.

	Filer identifieras endast med sökväg, inte med inode.

	`content`-filen modifieras INTE.
	`parity`-filerna modifieras vid behov.
	Filerna i arrayen modifieras vid behov.

  check
	Verifierar alla filer och paritetsdata.

	Det fungerar som `fix`, men det simulerar endast en återställning och inga ändringar
	skrivs till arrayen.

	Detta kommando är i första hand avsett för manuell verifiering,
	som efter en återställningsprocess eller under andra speciella förhållanden.
	För periodiska och schemalagda kontroller, använd `scrub`.

	Om du använder alternativet -a, --audit-only, kontrolleras endast fil-
	datan, och paritetsdatan ignoreras för en
	snabbare körning.

	Filer identifieras endast med sökväg, inte med inode.

	Inget ändras.

  list
	Listar alla filer som finns i arrayen vid tidpunkten för den
	senaste `sync`.

	Med -v eller --verbose visas även undertiden.

	Inget ändras.

  dup
	Listar alla dubblettfiler. Två filer antas vara lika om deras
	hashar matchar. Filens data läses inte; endast de
	förberäknade hasharna används.

	Inget ändras.

  pool
	Skapar eller uppdaterar en virtuell vy av alla
	filer i din disk-array i `pooling`-katalogen.

	Filerna kopieras inte utan länkas med hjälp av
	symboliska länkar.

	Vid uppdatering raderas alla befintliga symboliska länkar och tomma
	underkataloger och ersätts med den nya
	vyn av arrayen. Alla andra vanliga filer lämnas kvar.

	Inget ändras utanför pool-katalogen.

  devices
	Skriver ut de lågnivåenheter som används av arrayen.

	Detta kommando visar enhetsassociationerna i arrayen
	och är främst avsett som ett skriptgränssnitt.

	De två första kolumnerna är lågnivåenhetens ID och sökväg.
	De nästa två kolumnerna är högnivåenhetens ID och sökväg.
	Den sista kolumnen är diskens namn i arrayen.

	I de flesta fall har du en lågnivåenhet för varje disk i
	arrayen, men i vissa mer komplexa konfigurationer kan du ha flera
	lågnivåenheter som används av en enda disk i arrayen.

	Inget ändras.

  touch
	Anger en godtycklig undertidsstämpel för alla filer
	som har den satt till noll.

	Detta förbättrar SnapRAIDs förmåga att känna igen flyttade
	och kopierade filer, eftersom det gör tidsstämpeln nästan unik,
	vilket minskar möjliga dubbletter.

	Mer specifikt, om undertidsstämpeln inte är noll,
	identifieras en flyttad eller kopierad fil som sådan om den matchar
	namn, storlek och tidsstämpel. Om undertidsstämpeln
	är noll, betraktas den som en kopia endast om hela sökvägen,
	storlek och tidsstämpel alla matchar.

	Tidsstämpeln med sekundprecision modifieras inte,
	så alla datum och tider för dina filer bevaras.

  rehash
	Schemalägger en omhashning av hela arrayen.

	Detta kommando ändrar den hash-typ som används, vanligtvis vid uppgradering
	från ett 32-bitars system till ett 64-bitars system, för att byta från
	MurmurHash3 till den snabbare SpookyHash.

	Om du redan använder den optimala hashen, gör detta kommando
	ingenting och informerar dig om att ingen åtgärd behövs.

	Omhashningen utförs inte omedelbart utan sker
	progressivt under `sync` och `scrub`.

	Du kan kontrollera omhashningstillståndet med hjälp av `status`.

	Under omhashningen bibehåller SnapRAID full funktionalitet,
	med det enda undantaget att `dup` inte kan upptäcka dubbletter
	med hjälp av en annan hash.

Alternativ
	SnapRAID tillhandahåller följande alternativ:

	-c, --conf CONFIG
		Väljer den konfigurationsfil som ska användas. Om den inte specificeras, i Unix
		används filen `/usr/local/etc/snapraid.conf` om den finns,
		annars `/etc/snapraid.conf`.
		I Windows används filen `snapraid.conf` i samma
		katalog som `snapraid.exe`.

	-f, --filter PATTERN
		Filtrerar filerna att behandla i `check` och `fix`.
		Endast filerna som matchar det specificerade mönstret behandlas.
		Detta alternativ kan användas flera gånger.
		Se avsnittet MÖNSTER för mer information om
		mönsterspecifikationer.
		I Unix, se till att globbing-tecken citeras om de används.
		Detta alternativ kan endast användas med `check` och `fix`.
		Det kan inte användas med `sync` och `scrub`, eftersom de alltid
		behandlar hela arrayen.

	-d, --filter-disk NAME
		Filtrerar diskarna att behandla i `check`, `fix`, `up` och `down`.
		Du måste specificera ett disk-namn som definieras i konfigurations-
		filen.
		Du kan också specificera paritetsdiskar med namnen: `parity`, `2-parity`,
		`3-parity`, etc., för att begränsa operationer till en specifik paritetsdisk.
		Om du kombinerar flera --filter, --filter-disk och --filter-missing alternativ,
		väljs endast filer som matchar alla filter.
		Detta alternativ kan användas flera gånger.
		Detta alternativ kan endast användas med `check`, `fix`, `up` och `down`.
		Det kan inte användas med `sync` och `scrub`, eftersom de alltid
		behandlar hela arrayen.

	-m, --filter-missing
		Filtrerar filerna att behandla i `check` och `fix`.
		Endast filerna som saknas eller har raderats från arrayen behandlas.
		När det används med `fix`, fungerar detta som ett `undelete`-kommando.
		Om du kombinerar flera --filter, --filter-disk och --filter-missing alternativ,
		väljs endast filer som matchar alla filter.
		Detta alternativ kan endast användas med `check` och `fix`.
		Det kan inte användas med `sync` och `scrub`, eftersom de alltid
		behandlar hela arrayen.

	-e, --filter-error
		Behandlar filerna med fel i `check` och `fix`.
		Det behandlar endast filer som har block markerade med tysta
		eller input/output-fel under `sync` och `scrub`, som listas i `status`.
		Detta alternativ kan endast användas med `check` och `fix`.

	-p, --plan PERC|bad|new|full
		Väljer skrubbplanen. Om PERC är ett numeriskt värde från 0 till 100,
		tolkas det som procentandelen av block som ska skrubbas.
		Istället för en procentsats kan du specificera en plan:
		`bad` skrubbar dåliga block, `new` skrubbar block som ännu inte skrubbats,
		och `full` skrubbar allt.
		Detta alternativ kan endast användas med `scrub`.

	-o, --older-than DAYS
		Väljer den äldsta delen av arrayen att behandla i `scrub`.
		DAYS är den minsta åldern i dagar för ett block att skrubbas;
		standard är 10.
		Block markerade som dåliga skrubbas alltid oavsett detta alternativ.
		Detta alternativ kan endast användas med `scrub`.

	-a, --audit-only
		I `check`, verifierar hashen av filerna utan
		att kontrollera paritetsdata.
		Om du bara är intresserad av att kontrollera filens data, kan detta
		alternativ avsevärt påskynda kontrollprocessen.
		Detta alternativ kan endast användas med `check`.

	-h, --pre-hash
		I `sync`, kör en preliminär hash-fas av all ny data
		för ytterligare verifiering innan paritetsberäkningen.
		Vanligtvis görs ingen preliminär hashning i `sync`, och den nya
		datan hashberäknas precis innan paritetsberäkningen när den läses
		för första gången.
		Denna process sker när systemet är under
		tung belastning, med alla diskar snurrande och en upptagen CPU.
		Detta är ett extremt tillstånd för maskinen, och om den har ett
		latent hårdvaruproblem, kan tysta fel gå obemärkt förbi
		eftersom datan ännu inte har hashberäknats.
		För att undvika denna risk kan du aktivera `pre-hash`-läget för att få
		all data läst två gånger för att säkerställa dess integritet.
		Detta alternativ verifierar också filer som flyttats inom arrayen
		för att säkerställa att flyttoperationen lyckades och, om nödvändigt,
		låter dig köra en fix-operation innan du fortsätter.
		Detta alternativ kan endast användas med `sync`.

	-i, --import DIR
		Importerar från den specificerade katalogen alla filer som raderats
		från arrayen efter den senaste `sync`.
		Om du fortfarande har sådana filer, kan de användas av `check`
		och `fix` för att förbättra återställningsprocessen.
		Filerna läses, inklusive i underkataloger, och identifieras
		oavsett deras namn.
		Detta alternativ kan endast användas med `check` och `fix`.

	-s, --spin-down-on-error
		Vid något fel, snurrar ner alla hanterade diskar innan den avslutas med
		en statuskod som inte är noll. Detta förhindrar att enheterna
		förblir aktiva och snurrar efter en avbruten operation,
		vilket hjälper till att undvika onödig värmeuppbyggnad och ström-
		förbrukning. Använd detta alternativ för att säkerställa att diskar stoppas säkert
		även när ett kommando misslyckas.

	-w, --bw-limit RATE
		Tillämpar en global bandbreddsbegränsning för alla diskar. RATE är
		antalet byte per sekund. Du kan specificera en multiplikator
		som K, M eller G (t.ex. --bw-limit 1G).

	-A, --stats
		Aktiverar en utökad statusvy som visar ytterligare information.
		Skärmen visar två diagram:
		Det första diagrammet visar antalet buffrade strippar för varje
		disk, tillsammans med filsökvägen för filen som för närvarande används
		på den disken. Vanligtvis kommer den långsammaste disken att ha
		ingen buffert tillgänglig, vilket bestämmer den maximalt uppnåeliga
		bandbredden.
		Det andra diagrammet visar procentandelen tid som spenderats i väntan
		under de senaste 100 sekunderna. Den långsammaste disken förväntas
		orsaka större delen av väntetiden, medan andra diskar bör ha
		liten eller ingen väntetid eftersom de kan använda sina buffrade strippar.
		Detta diagram visar också den tid som spenderats i väntan på hash-
		beräkningar och RAID-beräkningar.
		Alla beräkningar körs parallellt med diskoperationer.
		Därför, så länge det finns mätbar väntetid för minst en disk,
		indikerar det att CPU:n är snabb nog att
		hålla jämna steg med arbetsbelastningen.

	-Z, --force-zero
		Tvingar den osäkra operationen att synkronisera en fil med noll
		storlek som tidigare var icke-noll.
		Om SnapRAID upptäcker ett sådant tillstånd, stoppar det att fortsätta
		om du inte specificerar detta alternativ.
		Detta gör att du enkelt kan upptäcka när, efter en systemkrasch,
		vissa filer som användes trunkerades.
		Detta är ett möjligt tillstånd i Linux med filsystemen ext3/ext4.
		Detta alternativ kan endast användas med `sync`.

	-E, --force-empty
		Tvingar den osäkra operationen att synkronisera en disk där alla
		ursprungliga filer saknas.
		Om SnapRAID upptäcker att alla filer som ursprungligen fanns
		på disken saknas eller har skrivits om, stoppar det att fortsätta
		om du inte specificerar detta alternativ.
		Detta gör att du enkelt kan upptäcka när ett datafilsystem inte är
		monterat.
		Detta alternativ kan endast användas med `sync`.

	-U, --force-uuid
		Tvingar den osäkra operationen att synkronisera, kontrollera och fixa
		med diskar som har ändrat sin UUID.
		Om SnapRAID upptäcker att vissa diskar har ändrat UUID,
		stoppar det att fortsätta om du inte specificerar detta alternativ.
		Detta gör att du kan upptäcka när dina diskar är monterade på fel
		monteringspunkter.
		Det är dock tillåtet att ha en enda UUID-ändring med
		enkel paritet, och fler med multipel paritet, eftersom detta är
		det normala fallet när man byter ut diskar efter en återställning.
		Detta alternativ kan endast användas med `sync`, `check` eller
		`fix`.

	-D, --force-device
		Tvingar den osäkra operationen att fixa med otillgängliga diskar
		eller med diskar på samma fysiska enhet.
		Till exempel, om du förlorade två datadisker och har en reservdisk för att återställa
		endast den första, kan du ignorera den andra otillgängliga disken.
		Eller, om du vill återställa en disk i det lediga utrymme som finns kvar på en
		redan använd disk, dela samma fysiska enhet.
		Detta alternativ kan endast användas med `fix`.

	-N, --force-nocopy
		I `sync`, `check` och `fix`, inaktiverar heuristiken för kopieringsdetektering.
		Utan detta alternativ antar SnapRAID att filer med samma
		attribut, såsom namn, storlek och tidsstämpel, är kopior med
		samma data.
		Detta möjliggör identifiering av kopierade eller flyttade filer från en disk
		till en annan och återanvänder den redan beräknade hash-informationen
		för att upptäcka tysta fel eller för att återställa saknade filer.
		I vissa sällsynta fall kan detta beteende leda till falska positiva
		eller en långsam process på grund av många hash-verifieringar, och detta
		alternativ låter dig lösa sådana problem.
		Detta alternativ kan endast användas med `sync`, `check` och `fix`.

	-F, --force-full
		I `sync`, tvingar en fullständig omberäkning av pariteten.
		Detta alternativ kan användas när du lägger till en ny paritetsnivå eller om
		du återgick till en gammal content-fil med mer aktuell paritetsdata.
		Istället för att återskapa pariteten från grunden låter detta dig
		återanvända hasharna som finns i content-filen för att validera data
		och bibehålla dataskyddet under `sync`-processen med hjälp av
		den befintliga paritetsdatan.
		Detta alternativ kan endast användas med `sync`.

	-R, --force-realloc
		I `sync`, tvingar en fullständig omallokering av filer och ombyggnad av pariteten.
		Detta alternativ kan användas för att helt omallokera alla filer,
		ta bort fragmentering, samtidigt som hasharna som finns i content-
		filen återanvänds för att validera data.
		Detta alternativ kan endast användas med `sync`.
		VARNING! Detta alternativ är endast för experter, och det rekommenderas
		starkt att inte använda det.
		Du HAR INGET dataskydd under `sync`-operationen.

	-l, --log FILE
		Skriver en detaljerad logg till den specificerade filen.
		Om detta alternativ inte specificeras, skrivs oväntade fel ut
		på skärmen, vilket potentiellt kan leda till överdriven utdata i händelse av
		många fel. När -l, --log specificeras, skrivs endast
		fatala fel som får SnapRAID att stoppa ut
		på skärmen.
		Om sökvägen börjar med `>>`, öppnas filen
		i tilläggsläge. Förekomster av `%D` och `%T` i namnet ersätts
		med datum och tid i formatet YYYYMMDD respektive
		HHMMSS. I Windows batch-filer måste du dubblera
		`%`-tecknet, t.ex. result-%%D.log. För att använda `>>` måste du
		omsluta namnet i citattecken, t.ex. `">>result.log"`.
		För att skicka ut loggen till standardutdata eller standardfel,
		kan du använda `">&1"` respektive `">&2"`.
		Se filen snapraid_log.txt eller man-sidan för beskrivningar av logg-taggar.

	-L, --error-limit NUMBER
		Anger en ny felgräns innan körningen stoppas.
		Som standard stoppar SnapRAID om det stöter på mer än 100
		input/output-fel, vilket indikerar att en disk troligen håller på att misslyckas.
		Detta alternativ påverkar `sync` och `scrub`, som tillåts
		att fortsätta efter den första uppsättningen diskfel för att försöka
		slutföra sina operationer.
		Däremot stoppar `check` och `fix` alltid vid det första felet.

	-S, --start BLKSTART
		Börjar behandlingen från det specificerade
		blocknumret. Detta kan vara användbart för att försöka igen att kontrollera
		eller fixa specifika block i händelse av en skadad disk.
		Detta alternativ är främst för avancerad manuell återställning.

	-B, --count BLKCOUNT
		Behandlar endast det specificerade antalet block.
		Detta alternativ är främst för avancerad manuell återställning.

	-C, --gen-conf CONTENT
		Genererar en dummy-konfigurationsfil från en befintlig
		content-fil.
		Konfigurationsfilen skrivs till standardutdata
		och skriver inte över en befintlig fil.
		Denna konfigurationsfil innehåller också den information
		som behövs för att rekonstruera diskmonteringspunkterna om du
		förlorar hela systemet.

	-v, --verbose
		Skriver ut mer information på skärmen.
		Om det specificeras en gång, skrivs exkluderade filer
		och ytterligare statistik ut.
		Detta alternativ har ingen effekt på loggfilerna.

	-q, --quiet
		Skriver ut mindre information på skärmen.
		Om det specificeras en gång, tas förloppsindikatorn bort; två gånger,
		de pågående operationerna; tre gånger, informations-
		meddelandena; fyra gånger, statusmeddelandena.
		Fatala fel skrivs alltid ut på skärmen.
		Detta alternativ har ingen effekt på loggfilerna.

	-H, --help
		Skriver ut en kort hjälpskärm.

	-V, --version
		Skriver ut programversionen.

Konfiguration
	SnapRAID kräver en konfigurationsfil för att veta var din disk-array
	finns och var paritetsinformationen ska lagras.

	I Unix används filen `/usr/local/etc/snapraid.conf` om den finns,
	annars `/etc/snapraid.conf`.
	I Windows används filen `snapraid.conf` i samma
	katalog som `snapraid.exe`.

	Den måste innehålla följande alternativ (skiftlägeskänsligt):

  parity FIL [,FIL] ...
	Definierar de filer som ska användas för att lagra paritetsinformationen.
	Pariteten möjliggör skydd från ett enda disk-
	fel, liknande RAID5.

	Du kan specificera flera filer, som måste finnas på olika diskar.
	När en fil inte kan växa mer, används nästa fil.
	Det totala tillgängliga utrymmet måste vara minst lika stort som den största datadisken i
	arrayen.

	Du kan lägga till ytterligare paritetsfiler senare, men du
	kan inte ändra ordning på eller ta bort dem.

	Att hålla paritetsdiskarna reserverade för paritet säkerställer att
	de inte blir fragmenterade, vilket förbättrar prestandan.

	I Windows lämnas 256 MB oanvänt på varje disk för att undvika
	varningen om fulla diskar.

	Detta alternativ är obligatoriskt och kan endast användas en gång.

  (2,3,4,5,6)-parity FIL [,FIL] ...
	Definierar de filer som ska användas för att lagra extra paritetsinformation.

	För varje paritetsnivå som specificeras aktiveras en ytterligare skyddsnivå:

	* 2-parity möjliggör RAID6 dubbel paritet.
	* 3-parity möjliggör trippel paritet.
	* 4-parity möjliggör fyrdubbel (fyra) paritet.
	* 5-parity möjliggör penta (fem) paritet.
	* 6-parity möjliggör hexa (sex) paritet.

	Varje paritetsnivå kräver närvaro av alla föregående paritets-
	nivåer.

	Samma överväganden som för alternativet `parity` gäller.

	Dessa alternativ är valfria och kan endast användas en gång.

  z-parity FIL [,FIL] ...
	Definierar en alternativ fil och ett format för att lagra trippel paritet.

	Detta alternativ är ett alternativ till `3-parity`, främst avsett för
	lågpresterande processorer som ARM eller AMD Phenom, Athlon och Opteron som inte
	stöder SSSE3-instruktionsuppsättningen. I sådana fall ger det
	bättre prestanda.

	Detta format liknar, men är snabbare än, det som används av ZFS RAIDZ3.
	Liksom ZFS fungerar det inte utöver trippel paritet.

	När du använder `3-parity` kommer du att varnas om det rekommenderas att använda
	formatet `z-parity` för prestandaförbättring.

	Det är möjligt att konvertera från ett format till ett annat genom att justera
	konfigurationsfilen med önskad z-parity eller 3-parity fil
	och använda `fix` för att återskapa den.

  content FIL
	Definierar den fil som ska användas för att lagra listan och checksummorna för alla
	filer som finns i din disk-array.

	Den kan placeras på en disk som används för data, paritet eller
	någon annan tillgänglig disk.
	Om du använder en datadisk exkluderas denna fil automatiskt
	från `sync`-processen.

	Detta alternativ är obligatoriskt och kan användas flera gånger för att spara
	flera kopior av samma fil.

	Du måste lagra minst en kopia för varje paritetsdisk som används
	plus en. Att använda ytterligare kopior skadar inte.

  data NAMN KATALOG
	Definierar namnet och monteringspunkten för datadiskarna i
	arrayen. NAMN används för att identifiera disken och måste
	vara unikt. KATALOG är monteringspunkten för disken i
	filsystemet.

	Du kan ändra monteringspunkten vid behov, så länge
	du behåller NAMN fixerat.

	Du bör använda ett alternativ för varje datadisk i arrayen.

	Du kan byta namn på en disk senare genom att ändra NAMN direkt
	i konfigurationsfilen och sedan köra ett `sync`-kommando.
	I händelse av namnbyte görs associationen med hjälp av den lagrade
	UUID:n för diskarna.

  nohidden
	Exkluderar alla dolda filer och kataloger.
	I Unix är dolda filer de som börjar med `.`.
	I Windows är de de med det dolda attributet.

  exclude/include MÖNSTER
	Definierar fil- eller katalogmönstren att exkludera eller inkludera
	i sync-processen.
	Alla mönster behandlas i den specificerade ordningen.

	Om det första mönstret som matchar är ett `exclude`-mönster,
	exkluderas filen. Om det är ett `include`-mönster, inkluderas filen.
	Om inget mönster matchar, exkluderas filen om det sista mönstret
	som specificerats är ett `include`-mönster, eller inkluderas om det sista mönstret
	som specificerats är ett `exclude`-mönster.

	Se avsnittet MÖNSTER för mer information om mönster-
	specifikationer.

	Detta alternativ kan användas flera gånger.

  blocksize STORLEK_I_KIBIBYTES
	Definierar den grundläggande blockstorleken i kibibytes för pariteten.
	En kibibyte är 1024 byte.

	Standard blockstorlek är 256, vilket bör fungera för de flesta fall.

	VARNING! Detta alternativ är endast för experter, och det rekommenderas
	starkt att inte ändra detta värde. För att ändra detta värde i framtiden,
	kommer du att behöva återskapa hela pariteten!

	En anledning att använda en annan blockstorlek är om du har många små
	filer, i storleksordningen miljoner.

	För varje fil, även om den bara är några byte, allokeras ett helt block paritet,
	och med många filer kan detta leda till betydande oanvänt paritetsutrymme.
	När du helt fyller paritetsdisken, är du inte
	tillåten att lägga till fler filer på datadiskarna.
	Det bortkastade paritetsutrymmet ackumuleras dock inte över datadiskar.
	Bortkastat utrymme som härrör från ett högt antal filer på en datadisk begränsar endast
	mängden data på den datadisken, inte andra.

	Som en approximation kan du anta att hälften av blockstorleken är
	bortkastad för varje fil. Till exempel, med 100 000 filer och en 256 KiB
	blockstorlek, kommer du att slösa 12,8 GB paritet, vilket kan resultera
	i 12,8 GB mindre utrymme tillgängligt på datadisken.

	Du kan kontrollera mängden bortkastat utrymme på varje disk med hjälp av `status`.
	Detta är mängden utrymme du måste lämna ledigt på data-
	diskarna eller använda för filer som inte ingår i arrayen.
	Om detta värde är negativt betyder det att du är nära att fylla
	pariteten, och det representerar det utrymme du fortfarande kan slösa.

	För att undvika detta problem kan du använda en större partition för paritet.
	Om till exempel paritetspartitionen är 12,8 GB större än datadiskarna,
	har du tillräckligt med extra utrymme för att hantera upp till 100 000
	filer på varje datadisk utan något bortkastat utrymme.

	Ett trick för att få en större paritetspartition i Linux är att formatera den
	med kommandot:

		:mkfs.ext4 -m 0 -T largefile4 DEVICE

	Detta resulterar i cirka 1,5% extra utrymme, ungefär 60 GB för
	en 4 TB-disk, vilket möjliggör cirka 460 000 filer på varje datadisk utan
	något bortkastat utrymme.

  hashsize STORLEK_I_BYTE
	Definierar hash-storleken i byte för de sparade blocken.

	Standard hash-storlek är 16 byte (128 bitar), vilket bör fungera
	för de flesta fall.

	VARNING! Detta alternativ är endast för experter, och det rekommenderas
	starkt att inte ändra detta värde. För att ändra detta värde i framtiden,
	kommer du att behöva återskapa hela pariteten!

	En anledning att använda en annan hash-storlek är om ditt system har
	begränsat minne. Som en tumregel kräver SnapRAID vanligtvis
	1 GiB RAM för varje 16 TB data i arrayen.

	Specifikt, för att lagra hasharna av datan, kräver SnapRAID
	ungefär TS*(1+HS)/BS byte RAM,
	där TS är den totala storleken i byte av din disk-array, BS är
	blockstorleken i byte och HS är hash-storleken i byte.

	Till exempel, med 8 diskar på 4 TB, en blockstorlek på 256 KiB
	(1 KiB = 1024 byte) och en hash-storlek på 16, får du:

	:RAM = (8 * 4 * 10^12) * (1+16) / (256 * 2^10) = 1,93 GiB

	Om du byter till en hash-storlek på 8 får du:

	:RAM = (8 * 4 * 10^12) * (1+8) / (256 * 2^10) = 1,02 GiB

	Om du byter till en blockstorlek på 512 får du:

	:RAM = (8 * 4 * 10^12) * (1+16) / (512 * 2^10) = 0,96 GiB

	Om du byter till både en hash-storlek på 8 och en blockstorlek på 512 får du:

	:RAM = (8 * 4 * 10^12) * (1+8) / (512 * 2^10) = 0,51 GiB

  autosave STORLEK_I_GIGABYTES
	Sparar automatiskt tillståndet vid synkronisering eller skrubbning efter att den
	specificerade mängden GB har behandlats.
	Detta alternativ är användbart för att undvika att starta om långa `sync`-
	kommandon från början om de avbryts av en maskinkrasch eller någon annan händelse.

  temp_limit TEMPERATUR_CELSIUS
	Anger den maximalt tillåtna disk-temperaturen i Celsius. När den specificeras,
	kontrollerar SnapRAID periodiskt temperaturen på alla diskar med hjälp av
	verktyget smartctl. De aktuella disk-temperaturerna visas medan
	SnapRAID är igång. Om någon disk överskrider denna gräns, stoppas alla operationer
	och diskarna snurras ner (sätts i standby) under den tid
	som definieras av alternativet `temp_sleep`. Efter viloperioden återupptas operationerna,
	och kan potentiellt pausas igen om temperaturgränsen nås
	ännu en gång.

	Under drift analyserar SnapRAID också värmekurvan för varje
	disk och uppskattar den långsiktiga stabila temperaturen de förväntas
	nå om aktiviteten fortsätter. Uppskattningen utförs först efter att
	disk-temperaturen har ökat fyra gånger, vilket säkerställer att tillräckligt med
	datapoäng finns tillgängliga för att fastställa en pålitlig trend.
	Denna förutsagda stabila temperatur visas inom parentes bredvid den
	aktuella värdet och hjälper till att bedöma om systemets kylning är
	tillräcklig. Denna uppskattade temperatur är endast för informationssyfte
	och har ingen effekt på SnapRAIDs beteende. Programmens
	åtgärder baseras enbart på de faktiska uppmätta disk-temperaturerna.

	För att utföra denna analys behöver SnapRAID en referens för system-
	temperaturen. Det försöker först läsa den från tillgängliga hårdvaru-
	sensorer. Om ingen system-sensor kan nås, används den lägsta disk-
	temperaturen som mättes vid start av körningen som en reservreferens.

	Normalt visar SnapRAID endast temperaturen på den hetaste disken.
	För att visa temperaturen på alla diskar, använd alternativet -A eller --stats.

  temp_sleep TID_I_MINUTER
	Anger standby-tiden, i minuter, när temperaturgränsen har
	nåtts. Under denna period förblir diskarna nedsnurrade. Standard
	är 5 minuter.

  pool KATALOG
	Definierar poolningskatalogen där den virtuella vyn av disk-
	arrayen skapas med hjälp av kommandot `pool`.

	Katalogen måste redan existera.

  share UNC_KATALOG
	Definierar Windows UNC-sökvägen som krävs för att få åtkomst till diskarna på distans.

	Om detta alternativ specificeras, använder de symboliska länkarna som skapats i pool-
	katalogen denna UNC-sökväg för att få åtkomst till diskarna.
	Utan detta alternativ använder de genererade symboliska länkarna endast lokala sökvägar,
	vilket inte tillåter delning av pool-katalogen över nätverket.

	De symboliska länkarna bildas med hjälp av den specificerade UNC-sökvägen, lägger till
	disk-namnet som specificeras i `data`-alternativet, och lägger slutligen till filens
	katalog och namn.

	Detta alternativ krävs endast för Windows.

  smartctl DISK/PARITY ALTERNATIV...
	Definierar anpassade smartctl-alternativ för att få SMART-attributen för
	varje disk. Detta kan krävas för RAID-kontroller och vissa USB-
	diskar som inte kan upptäckas automatiskt. Platsinnehavaren %s ersätts av
	enhetens namn, men är valfri för fixerade enheter som RAID-kontroller.

	DISK är samma disk-namn som specificeras i `data`-alternativet.
	PARITY är ett av paritetsnamnen: `parity`, `2-parity`, `3-parity`,
	`4-parity`, `5-parity`, `6-parity` eller `z-parity`.

	I de specificerade ALTERNATIVEN ersätts strängen `%s` med
	enhetens namn. För RAID-kontroller är enheten
	sannolikt fixerad, och du kanske inte behöver använda `%s`.

	Se smartmontools dokumentation för möjliga alternativ:

		:https://www.smartmontools.org/wiki/Supported_RAID-Controllers
		:https://www.smartmontools.org/wiki/Supported_USB-Devices

	Till exempel:

		:smartctl parity -d sat %s

  smartignore DISK/PARITY ATTR [ATTR...]
	Ignorerar det specificerade SMART-attributet vid beräkning av sannolikheten
	för diskfel. Detta alternativ är användbart om en disk rapporterar ovanliga eller
	vilseledande värden för ett visst attribut.

	DISK är samma disk-namn som specificeras i `data`-alternativet.
	PARITY är ett av paritetsnamnen: `parity`, `2-parity`, `3-parity`,
	`4-parity`, `5-parity`, `6-parity` eller `z-parity`.
	Det speciella värdet * kan användas för att ignorera attributet på alla diskar.

	Till exempel, för att ignorera attributet `Current Pending Sector Count` på
	alla diskar:

		:smartignore * 197

	För att ignorera det endast på den första paritetsdisken:

		:smartignore parity 197

  Exempel
	Ett exempel på en typisk konfiguration för Unix är:

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

	Ett exempel på en typisk konfiguration för Windows är:

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

Mönster (Pattern)
	Mönster ger ett flexibelt sätt att filtrera filer för inkludering eller
	exkludering. Genom att använda jokertecken kan du definiera regler som
	matchar specifika filnamn eller hela katalogstrukturer utan att
	lista varje sökväg manuellt.

	Frågetecknet `?` matchar valfritt enskilt tecken utom katalogavgränsaren.
	Detta gör det användbart för att matcha filnamn med variabla tecken
	samtidigt som mönstret hålls begränsat till en enda katalognivå.

	Den enkla stjärnan `*` matchar valfri teckensekvens, men precis som
	frågetecknet korsar den aldrig kataloggränser. Den stannar vid
	snedstrecket, vilket gör den lämplig för matchning inom en enskild
	sökvägskomponent. Detta är det standardbeteende för jokertecken som är
	bekant från skal-globbing.

	Den dubbla stjärnan `**` är mer kraftfull; den matchar valfri teckensekvens
	inklusive katalogavgränsare. Detta gör att mönster kan matcha över
	flera katalognivåer. När `**` förekommer direkt i ett mönster kan det
	matcha noll eller fler tecken inklusive snedstreck mellan den
	omkringliggande bokstavliga texten.

	Den viktigaste användningen av `**` är i den speciella formen `/**/`. Detta matchar
	noll eller flera kompletta katalognivåer, vilket gör det möjligt att matcha filer
	på valfritt djup i ett katalogträd utan att känna till den exakta sökvägsstrukturen.
	Till exempel matchar mönstret `src/**/main.js` filerna `src/main.js` (hoppar över
	noll kataloger), `src/ui/main.js` (hoppar över en katalog) och
	`src/ui/components/main.js` (hoppar över två kataloger).

	Teckenklasser som använder hakparenteser `[]` matchar ett enskilt tecken från en
	angiven uppsättning eller ett intervall. Precis som de andra mönstren för enskilda
	tecken matchar de inte katalogavgränsare. Klasser stöder intervall och
	negering med ett utropstecken.

	Den grundläggande skillnaden att komma ihåg är att `*`, `?` och teckenklasser
	alla respekterar kataloggränser och endast matchar inom en enskild
	sökvägskomponent, medan `**` är det enda mönstret som kan matcha över
	katalogavgränsare.

	Det finns fyra olika typer av mönster:

	=FILE
		Väljer valfri fil med namnet FILE.
		Detta mönster gäller endast filer, inte kataloger.

	=DIR/
		Väljer valfri katalog med namnet DIR och allt innehåll.
		Detta mönster gäller endast kataloger, inte filer.

	=/PATH/FILE
		Väljer den exakt angivna filsökvägen. Detta mönster gäller
		endast filer, inte kataloger.

	=/PATH/DIR/
		Väljer den exakt angivna katalogsökvägen och allt innehåll.
		Detta mönster gäller endast kataloger, inte filer.

	När du specificerar en absolut sökväg som börjar med /, tillämpas den vid
	array-rotkatalogen, inte den lokala filsystemets rotkatalog.

	I Windows kan du använda bakåtsnedstrecket \ istället för framåtsnedstrecket /.
	Windows systemkataloger, junction points, monteringspunkter och andra Windows
	speciella kataloger behandlas som filer, vilket innebär att för att exkludera
	dem måste du använda en fil-regel, inte en katalog-regel.

	Om filnamnet innehåller tecknet `*`, `?`, `[`,
	eller `]`, måste du undvika det för att förhindra att det tolkas som ett
	globbing-tecken. I Unix är undvikande-tecknet `\`; i Windows är det `^`.
	När mönstret är på kommandoraden måste du dubbla undvikande-
	tecknet för att förhindra att det tolkas av kommandoskalet.

	I konfigurationsfilen kan du använda olika strategier för att filtrera
	filerna som ska behandlas.
	Det enklaste tillvägagångssättet är att endast använda `exclude`-regler för att ta bort alla
	filer och kataloger du inte vill behandla. Till exempel:

		:# Exkluderar alla filer som heter `*.unrecoverable`
		:exclude *.unrecoverable
		:# Exkluderar rotkatalogen `/lost+found`
		:exclude /lost+found/
		:# Exkluderar alla underkataloger som heter `tmp`
		:exclude tmp/

	Det motsatta tillvägagångssättet är att definiera endast de filer du vill behandla, med
	endast `include`-regler. Till exempel:

		:# Inkluderar endast vissa kataloger
		:include /movies/
		:include /musics/
		:include /pictures/

	Det sista tillvägagångssättet är att blanda `exclude`- och `include`-regler. I detta fall
	är ordningen på reglerna viktig. Tidigare regler har
	företräde framför senare.
	För att förenkla kan du lista alla `exclude`-regler först och sedan
	alla `include`-regler. Till exempel:

		:# Exkluderar alla filer som heter `*.unrecoverable`
		:exclude *.unrecoverable
		:# Exkluderar alla underkataloger som heter `tmp`
		:exclude tmp/
		:# Inkluderar endast vissa kataloger
		:include /movies/
		:include /musics/
		:include /pictures/

	På kommandoraden, med hjälp av alternativet -f, kan du bara använda `include`-
	mönster. Till exempel:

		:# Kontrollerar endast .mp3-filerna.
		:# I Unix, använd citattecken för att undvika globbing-expansion av skalet.
		:snapraid -f "*.mp3" check

	I Unix, när du använder globbing-tecken på kommandoraden, måste du
	citattecken dem för att förhindra att skalet expanderar dem.

Ignorera filer (Ignore File)
	Utöver de globala reglerna i konfigurationsfilen kan du placera
	`.snapraidignore`-filer i vilken katalog som helst i arrayen för att definiera
	decentraliserade exkluderingsregler.

	Regler som definieras i `.snapraidignore` tillämpas efter reglerna i
	konfigurationsfilen. Detta innebär att de har högre prioritet och kan
	användas för att exkludera filer som tidigare inkluderats av den globala
	konfigurationen. I praktiken innebär det att om en lokal regel matchar, så
	exkluderas filen oavsett de globala inkluderingsinställningarna.

	Mönsterlogiken i `.snapraidignore` speglar den globala konfigurationen men
	förankrar mönstren till den katalog där filen finns:

	=FILE
		Väljer alla filer med namnet FILE i denna katalog eller under.
		Detta följer samma globbing-regler som det globala mönstret.

	=DIR/
		Väljer alla kataloger med namnet DIR och allt innehåll, som finns
		i denna katalog eller under.

	=/PATH/FILE
		Väljer den exakt angivna filen relativt platsen för
		`.snapraidignore`-filen.

	=/PATH/DIR/
		Väljer den exakt angivna katalogen och allt innehåll, relativt
		platsen för `.snapraidignore`-filen.

	Till skillnad från den globala konfigurationen stöder `.snapraidignore`-filer
	endast exkluderingsregler; du kan inte använda `include`-mönster eller negation (!).

	Till exempel, om du har en `.snapraidignore` i `/mnt/disk1/projects/`:

		:# Exkluderar ENDAST /mnt/disk1/projects/output.bin
		:/output.bin
		:# Exkluderar alla kataloger med namnet `build` inuti projects/
		:build/
		:# Exkluderar alla .tmp-filer inuti projects/ eller dess undermappar
		:*.tmp

Innehåll (Content)
	SnapRAID lagrar listan och checksummorna för dina filer i content-filen.

	Det är en binär fil som listar alla filer som finns i din disk-array,
	tillsammans med alla checksummor för att verifiera deras integritet.

	Denna fil läses och skrivs av kommandona `sync` och `scrub` och
	läses av kommandona `fix`, `check` och `status`.

Paritet (Parity)
	SnapRAID lagrar paritetsinformationen för din array i parity-
	filerna.

	Dessa är binära filer som innehåller den beräknade pariteten för alla
	block definierade i `content`-filen.

	Dessa filer läses och skrivs av kommandona `sync` och `fix` och
	läses endast av kommandona `scrub` och `check`.

Kodning
	SnapRAID i Unix ignorerar all kodning. Det läser och lagrar
	filnamnen med samma kodning som används av filsystemet.

	I Windows konverteras alla namn som läses från filsystemet och
	behandlas i UTF-8-format.

	För att få filnamn utskrivna korrekt måste du ställa in Windows-
	konsolen till UTF-8-läge med kommandot `chcp 65001` och använda
	ett TrueType-typsnitt som `Lucida Console` som konsoltypsnitt.
	Detta påverkar endast de utskrivna filnamnen; om du
	omdirigerar konsolutdata till en fil, är den resulterande filen alltid
	i UTF-8-format.

Upphovsrätt (Copyright)
	Denna fil är Copyright (C) 2025 Andrea Mazzoleni

Se Även (See Also)
	snapraid_log(1), snapraidd(1)

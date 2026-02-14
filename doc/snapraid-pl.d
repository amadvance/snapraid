Name{number}
	snapraid - Kopia zapasowa SnapRAID dla macierzy dyskowych

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
	SnapRAID to program do tworzenia kopii zapasowych przeznaczony
	dla macierzy dyskowych, przechowujący informacje parzystości
	do odzyskiwania danych w przypadku awarii do sześciu dysków.

	Przeznaczony głównie dla domowych centrów multimedialnych z dużymi,
	rzadko zmieniającymi się plikami, SnapRAID oferuje kilka funkcji:

	* Możesz używać dysków już wypełnionych plikami bez
		konieczności ich ponownego formatowania, uzyskując do nich dostęp jak zwykle.
	* Wszystkie Twoje dane są haszowane, aby zapewnić integralność danych i zapobiec
		cichej korupcji.
	* Gdy liczba uszkodzonych dysków przekracza liczbę parzystości,
		utrata danych ogranicza się do dotkniętych dysków; dane na
		innych dyskach pozostają dostępne.
	* Jeśli przypadkowo usuniesz pliki na dysku, możliwe jest ich odzyskanie.
	* Dyski mogą mieć różne rozmiary.
	* Możesz dodawać dyski w dowolnym momencie.
	* SnapRAID nie blokuje Twoich danych; możesz przestać go używać
		w dowolnym momencie bez ponownego formatowania ani przenoszenia danych.
	* Aby uzyskać dostęp do pliku, wystarczy, że zakręci się tylko jeden dysk,
		co oszczędza energię i zmniejsza hałas.

	Więcej informacji można znaleźć na oficjalnej stronie SnapRAID:

		:https://www.snapraid.it/

Limitations
	SnapRAID to hybryda programu RAID i programu do tworzenia kopii zapasowych,
	której celem jest połączenie najlepszych zalet obu. Ma jednak pewne
	ograniczenia, które należy wziąć pod uwagę przed jego użyciem.

	Głównym ograniczeniem jest to, że jeśli dysk ulegnie awarii, a Ty nie wykonałeś
	niedawno synchronizacji, możesz nie być w stanie w pełni odzyskać danych.
	Dokładniej, możesz nie być w stanie odzyskać danych o wielkości
	zmienionych lub usuniętych plików od czasu ostatniej operacji synchronizacji.
	Dzieje się tak, nawet jeśli zmienione lub usunięte pliki nie znajdują się na
	uszkodzonym dysku. Dlatego SnapRAID jest lepiej dostosowany do
	danych, które rzadko się zmieniają.

	Z drugiej strony, nowo dodane pliki nie uniemożliwiają odzyskania już
	istniejących plików. Stracisz tylko ostatnio dodane pliki, jeśli
	znajdują się one na uszkodzonym dysku.

	Inne ograniczenia SnapRAID to:

	* W SnapRAID nadal masz oddzielne systemy plików dla każdego dysku.
		W RAID otrzymujesz jeden duży system plików.
	* SnapRAID nie rozdziela danych.
		W RAID uzyskujesz wzrost prędkości dzięki rozdzielaniu.
	* SnapRAID nie obsługuje odzyskiwania w czasie rzeczywistym.
		W RAID nie musisz przerywać pracy, gdy dysk ulegnie awarii.
	* SnapRAID może odzyskać dane tylko z ograniczonej liczby awarii dysków.
		W przypadku kopii zapasowej możesz odzyskać dane po całkowitej
		awarii całej macierzy dysków.
	* Zapisywane są tylko nazwy plików, znaczniki czasu, dowiązania symboliczne
		i dowiązania twarde.
		Uprawnienia, właścicielstwo i rozszerzone atrybuty nie są zapisywane.

Getting Started
	Aby użyć SnapRAID, musisz najpierw wybrać jeden dysk w swojej macierzy
	dysków, który zostanie przeznaczony na informacje o `parzystości`. Z jednym dyskiem
	na parzystość będziesz w stanie odzyskać dane po awarii pojedynczego dysku,
	podobnie jak w RAID5.

	Jeśli chcesz odzyskać dane po większej liczbie awarii dysków, podobnie jak
	w RAID6, musisz zarezerwować dodatkowe dyski na parzystość. Każdy dodatkowy
	dysk parzystości umożliwia odzyskanie danych po jeszcze jednej awarii dysku.

	Jako dyski parzystości musisz wybrać największe dyski w macierzy,
	ponieważ informacje o parzystości mogą urosnąć do rozmiaru największego
	dysku danych w macierzy.

	Dyski te będą przeznaczone do przechowywania plików `parzystości`.
	Nie powinieneś na nich przechowywać swoich danych.

	Następnie musisz zdefiniować dyski `danych`, które chcesz chronić
	za pomocą SnapRAID. Ochrona jest bardziej skuteczna, jeśli te dyski
	zawierają dane, które rzadko się zmieniają. Z tego powodu lepiej jest
	NIE włączać dysku C:\ w systemie Windows ani katalogów /home, /var i /tmp
	w systemie Unix.

	Lista plików jest zapisywana w plikach `zawartości`, zwykle
	przechowywanych na dyskach danych, parzystości lub rozruchowych.
	Plik ten zawiera szczegóły Twojej kopii zapasowej, w tym wszystkie
	sumy kontrolne w celu weryfikacji jej integralności.
	Plik `zawartości` jest przechowywany w wielu kopiach, a każda kopia musi
	znajdować się na innym dysku, aby zapewnić, że nawet w przypadku wielu
	awarii dysków, przynajmniej jedna kopia jest dostępna.

	Na przykład, załóżmy, że interesuje Cię tylko jeden poziom parzystości
	i Twoje dyski znajdują się w:

		:/mnt/diskp <- wybrany dysk dla parzystości
		:/mnt/disk1 <- pierwszy dysk do ochrony
		:/mnt/disk2 <- drugi dysk do ochrony
		:/mnt/disk3 <- trzeci dysk do ochrony

	Musisz utworzyć plik konfiguracyjny /etc/snapraid.conf z
	następującymi opcjami:

		:parity /mnt/diskp/snapraid.parity
		:content /var/snapraid/snapraid.content
		:content /mnt/disk1/snapraid.content
		:content /mnt/disk2/snapraid.content
		:data d1 /mnt/disk1/
		:data d2 /mnt/disk2/
		:data d3 /mnt/disk3/

	Jeśli używasz systemu Windows, powinieneś użyć formatu ścieżek Windows,
	z literami dysków i ukośnikami odwrotnymi zamiast ukośników:

		:parity E:\snapraid.parity
		:content C:\snapraid\snapraid.content
		:content F:\array\snapraid.content
		:content G:\array\snapraid.content
		:data d1 F:\array\
		:data d2 G:\array\
		:data d3 H:\array\

	Jeśli masz wiele dysków i zabrakło Ci liter dysków, możesz zamontować
	dyski bezpośrednio w podfolderach. Zobacz:

		:https://www.google.com/search?q=Windows+mount+point

	W tym momencie jesteś gotowy do uruchomienia polecenia `sync`, aby zbudować
	informacje o parzystości.

		:snapraid sync

	Ten proces może zająć kilka godzin za pierwszym razem, w zależności od rozmiaru
	danych już obecnych na dyskach. Jeśli dyski są puste,
	proces jest natychmiastowy.

	Możesz go zatrzymać w dowolnym momencie, naciskając Ctrl+C, a przy następnym
	uruchomieniu wznowi się w miejscu, w którym został przerwany.

	Kiedy to polecenie się zakończy, Twoje dane są BEZPIECZNE.

	Teraz możesz zacząć używać swojej macierzy, jak chcesz, i okresowo
	aktualizować informacje o parzystości, uruchamiając polecenie `sync`.

  Scrubbing
	Aby okresowo sprawdzać dane i parzystość pod kątem błędów, możesz
	uruchomić polecenie `scrub`.

		:snapraid scrub

	To polecenie porównuje dane w Twojej macierzy z haszem obliczonym
	podczas polecenia `sync`, aby zweryfikować integralność.

	Każde uruchomienie polecenia sprawdza około 8% macierzy, z wyłączeniem danych
	już sprawdzonych w ciągu poprzednich 10 dni.
	Możesz użyć opcji -p, --plan, aby określić inną ilość,
	oraz opcji -o, --older-than, aby określić inny wiek w dniach.
	Na przykład, aby sprawdzić 5% macierzy dla bloków starszych niż 20 dni, użyj:

		:snapraid -p 5 -o 20 scrub

	Jeśli podczas procesu zostaną znalezione ciche błędy lub błędy wejścia/wyjścia,
	odpowiednie bloki zostaną oznaczone jako złe w pliku `content`
	i wymienione w poleceniu `status`.

		:snapraid status

	Aby je naprawić, możesz użyć polecenia `fix`, filtrując złe bloki
	za pomocą opcji -e, --filter-error:

		:snapraid -e fix

	Przy następnym `scrub`, błędy znikną z raportu `status`,
	jeśli zostały faktycznie naprawione. Aby przyspieszyć, możesz użyć -p bad, aby
	sprawdzić tylko bloki oznaczone jako złe.

		:snapraid -p bad scrub

	Uruchomienie `scrub` na niezsynchronizowanej macierzy może zgłosić błędy
	spowodowane usuniętymi lub zmodyfikowanymi plikami. Błędy te są zgłaszane
	w wyniku `scrub`, ale powiązane bloki nie są oznaczane jako złe.

  Pooling
	Uwaga: Funkcja łączenia w pulę opisana poniżej została zastąpiona przez
	narzędzie mergefs, które jest obecnie zalecaną opcją dla użytkowników Linuksa
	w społeczności SnapRAID. Mergefs zapewnia bardziej elastyczny i wydajny
	sposób łączenia wielu dysków w jeden ujednolicony punkt montowania,
	umożliwiając bezproblemowy dostęp do plików w całej macierzy bez polegania
	na dowiązaniach symbolicznych. Dobrze integruje się z SnapRAID w celu ochrony
	parzystości i jest powszechnie używane w konfiguracjach takich jak
	OpenMediaVault (OMV) lub niestandardowe konfiguracje NAS.

	Aby wszystkie pliki w Twojej macierzy były widoczne w tym samym drzewie katalogów,
	możesz włączyć funkcję `pooling`. Tworzy ona wirtualny, tylko do odczytu,
	widok wszystkich plików w Twojej macierzy za pomocą dowiązań symbolicznych.

	Katalog `pooling` możesz skonfigurować w pliku konfiguracyjnym za pomocą:

		:pool /pool

	lub, jeśli używasz systemu Windows, za pomocą:

		:pool C:\pool

	a następnie uruchom polecenie `pool`, aby utworzyć lub zaktualizować wirtualny widok.

		:snapraid pool

	Jeśli używasz platformy Unix i chcesz udostępnić ten katalog
	przez sieć maszynom Windows lub Unix, powinieneś dodać
	następujące opcje do swojego /etc/samba/smb.conf:

		:# W sekcji globalnej smb.conf
		:unix extensions = no

		:# W sekcji udziału smb.conf
		:[pool]
		:comment = Pula
		:path = /pool
		:read only = yes
		:guest ok = yes
		:wide links = yes
		:follow symlinks = yes

	W systemie Windows udostępnianie dowiązań symbolicznych przez sieć wymaga od klientów
	zdalnego ich rozwiązywania. Aby to umożliwić, oprócz udostępnienia katalogu puli,
	musisz również udostępnić wszystkie dyski niezależnie, używając nazw dysków
	zdefiniowanych w pliku konfiguracyjnym jako punkty udostępniania. Musisz również
	określić w opcji `share` w pliku konfiguracyjnym ścieżkę UNC Windows,
	której klienci zdalni muszą użyć, aby uzyskać dostęp do tych udostępnionych dysków.

	Na przykład, działając z serwera o nazwie `darkstar`, możesz użyć
	opcji:

		:data d1 F:\array\
		:data d2 G:\array\
		:data d3 H:\array\
		:pool C:\pool
		:share \\darkstar

	i udostępnić w sieci następujące katalogi:

		:\\darkstar\pool -> C:\pool
		:\\darkstar\d1 -> F:\array
		:\\darkstar\d2 -> G:\array
		:\\darkstar\d3 -> H:\array

	aby umożliwić zdalnym klientom dostęp do wszystkich plików pod adresem \\darkstar\pool.

	Możesz również potrzebować skonfigurować klientów zdalnych, aby umożliwić dostęp
	do zdalnych dowiązań symbolicznych za pomocą polecenia:

		:fsutil behavior set SymlinkEvaluation L2L:1 R2R:1 L2R:1 R2L:1

  Undeleting
	SnapRAID działa bardziej jak program do tworzenia kopii zapasowych niż system RAID
	i może być użyty do przywrócenia lub odzyskania usuniętych plików do ich poprzedniego
	stanu za pomocą opcji -f, --filter:

		:snapraid fix -f FILE

	lub dla katalogu:

		:snapraid fix -f DIR/

	Możesz go również użyć do odzyskania tylko przypadkowo usuniętych plików wewnątrz
	katalogu za pomocą opcji -m, --filter-missing, która przywraca
	tylko brakujące pliki, pozostawiając wszystkie inne nienaruszone.

		:snapraid fix -m -f DIR/

	Lub aby odzyskać wszystkie usunięte pliki na wszystkich dyskach za pomocą:

		:snapraid fix -m

  Recovering
	Najgorsze się wydarzyło i straciłeś jeden lub więcej dysków!

	NIE PANIKUJ! Będziesz w stanie je odzyskać!

	Pierwszą rzeczą, którą musisz zrobić, to unikać dalszych zmian w Twojej macierzy dysków.
	Wyłącz wszelkie zdalne połączenia z nią i wszelkie zaplanowane procesy, w tym
	jakąkolwiek zaplanowaną nocną synchronizację lub scrub SnapRAID.

	Następnie postępuj zgodnie z poniższymi krokami.

    KROK 1 -> Ponowna konfiguracja
	Potrzebujesz trochę miejsca do odzyskania, najlepiej na dodatkowych
	zapasowych dyskach, ale wystarczy zewnętrzny dysk USB lub dysk zdalny.

	Zmodyfikuj plik konfiguracyjny SnapRAID, aby opcja `data` lub `parity`
	uszkodzonego dysku wskazywała na lokalizację z wystarczającą ilością pustego
	miejsca do odzyskania plików.

	Na przykład, jeśli dysk `d1` uległ awarii, zmień z:

		:data d1 /mnt/disk1/

	na:

		:data d1 /mnt/new_spare_disk/

	Jeśli dysk do odzyskania jest dyskiem parzystości, zaktualizuj odpowiednią
	opcję `parity`.
	Jeśli masz wiele uszkodzonych dysków, zaktualizuj wszystkie ich opcje konfiguracyjne.

    KROK 2 -> Naprawa
	Uruchom polecenie fix, zapisując log w pliku zewnętrznym za pomocą:

		:snapraid -d NAME -l fix.log fix

	Gdzie NAME to nazwa dysku, taka jak `d1` w naszym poprzednim przykładzie.
	Jeśli dysk do odzyskania jest dyskiem parzystości, użyj nazw `parity`, `2-parity`,
	itp.
	Jeśli masz wiele uszkodzonych dysków, użyj wielu opcji -d, aby określić
	wszystkie z nich.

	To polecenie zajmie dużo czasu.

	Upewnij się, że masz kilka gigabajtów wolnego miejsca na zapisanie pliku fix.log.
	Uruchom je z dysku z wystarczającą ilością wolnego miejsca.

	Teraz odzyskałeś wszystko, co jest możliwe do odzyskania. Jeśli niektóre pliki
	są częściowo lub całkowicie nie do odzyskania, zostaną one przemianowane
	poprzez dodanie rozszerzenia `.unrecoverable`.

	Szczegółową listę wszystkich nie do odzyskania bloków znajdziesz w pliku fix.log,
	sprawdzając wszystkie linie zaczynające się od `unrecoverable:`.

	Jeśli nie jesteś zadowolony z odzyskiwania, możesz spróbować ponownie
	tyle razy, ile chcesz.

	Na przykład, jeśli usunąłeś pliki z macierzy po ostatniej
	`sync`, może to spowodować, że niektóre pliki nie zostaną odzyskane.
	W tym przypadku możesz ponowić próbę `fix` za pomocą opcji -i, --import,
	określając, gdzie te pliki znajdują się teraz, aby włączyć je ponownie
	do procesu odzyskiwania.

	Jeśli jesteś zadowolony z odzyskiwania, możesz kontynuować,
	ale pamiętaj, że po synchronizacji nie możesz już ponowić próby polecenia `fix`!

    KROK 3 -> Sprawdzenie
	Jako ostrożne sprawdzenie, możesz teraz uruchomić polecenie `check`, aby upewnić się, że
	wszystko jest poprawne na odzyskanym dysku.

		:snapraid -d NAME -a check

	Gdzie NAME to nazwa dysku, taka jak `d1` w naszym poprzednim przykładzie.

	Opcje -d i -a mówią SnapRAID, aby sprawdził tylko określony dysk
	i zignorował wszystkie dane parzystości.

	To polecenie zajmie dużo czasu, ale jeśli nie jesteś zbyt ostrożny,
	możesz je pominąć.

    KROK 4 -> Synchronizacja
	Uruchom polecenie `sync`, aby ponownie zsynchronizować macierz z nowym dyskiem.

		:snapraid sync

	Jeśli wszystko zostanie odzyskane, to polecenie jest natychmiastowe.

Commands
	SnapRAID udostępnia kilka prostych poleceń, które pozwalają na:

	* Wyświetlenie statusu macierzy -> `status`
	* Kontrolę dysków -> `smart`, `probe`, `up`, `down`
	* Wykonanie kopii zapasowej/migawki -> `sync`
	* Okresowe sprawdzanie danych -> `scrub`
	* Przywrócenie ostatniej kopii zapasowej/migawki -> `fix`.

	Polecenia muszą być zapisane małymi literami.

  status
	Wyświetla podsumowanie stanu macierzy dysków.

	Zawiera informacje o fragmentacji parzystości, jak stare
	są bloki bez sprawdzenia i wszystkie zarejestrowane ciche
	błędy napotkane podczas scrubbingu.

	Prezentowane informacje odnoszą się do ostatniego momentu, w którym
	uruchomiłeś `sync`. Późniejsze modyfikacje nie są brane pod uwagę.

	Jeśli wykryto złe bloki, ich numery bloków są wymienione.
	Aby je naprawić, możesz użyć polecenia `fix -e`.

	Pokazuje również wykres reprezentujący ostatni raz, kiedy każdy blok
	został sprawdzony lub zsynchronizowany. Sprawdzone bloki są pokazane
	za pomocą `*`, bloki zsynchronizowane, ale jeszcze niesprawdzone, za pomocą `o`.

	Nic nie jest modyfikowane.

  smart
	Wyświetla raport SMART wszystkich dysków w systemie.

	Zawiera oszacowanie prawdopodobieństwa awarii w następnym
	roku, co pozwala zaplanować wymianę konserwacyjną dysków, które wykazują
	podejrzane atrybuty.

	To oszacowanie prawdopodobieństwa uzyskuje się poprzez skorelowanie atrybutów SMART
	dysków z danymi Backblaze dostępnymi pod adresem:

		:https://www.backblaze.com/hard-drive-test-data.html

	Jeśli SMART zgłosi, że dysk ulega awarii, dla tego dysku
	wyświetlane jest `FAIL` lub `PREFAIL`, a SnapRAID zwraca błąd.
	W takim przypadku natychmiastowa wymiana dysku jest wysoce zalecana.

	Inne możliwe ciągi statusu to:
		logfail - W przeszłości niektóre atrybuty były niższe niż
			próg.
		logerr - Dziennik błędów urządzenia zawiera błędy.
		selferr - Dziennik autotestu urządzenia zawiera błędy.

	Jeśli określono opcję -v, --verbose, dostarczana jest głębsza analiza
	statystyczna. Ta analiza może pomóc Ci zdecydować, czy potrzebujesz
	więcej czy mniej parzystości.

	To polecenie używa narzędzia `smartctl` i jest równoważne
	uruchomieniu `smartctl -a` na wszystkich urządzeniach.

	Jeśli Twoje urządzenia nie zostaną poprawnie wykryte automatycznie, możesz określić
	niestandardowe polecenie za pomocą opcji `smartctl` w pliku konfiguracyjnym.

	Nic nie jest modyfikowane.

  probe
	Wyświetla stan ZASILANIA wszystkich dysków w systemie.

	`Standby` oznacza, że dysk się nie kręci. `Active` oznacza,
	że dysk się kręci.

	To polecenie używa narzędzia `smartctl` i jest równoważne
	uruchomieniu `smartctl -n standby -i` na wszystkich urządzeniach.

	Jeśli Twoje urządzenia nie zostaną poprawnie wykryte automatycznie, możesz określić
	niestandardowe polecenie za pomocą opcji `smartctl` w pliku konfiguracyjnym.

	Nic nie jest modyfikowane.

  up
	Rozpędza wszystkie dyski macierzy.

	Możesz rozpędzić tylko określone dyski za pomocą opcji -d, --filter-disk.

	Rozpędzanie wszystkich dysków w tym samym czasie wymaga dużo energii.
	Upewnij się, że Twój zasilacz jest w stanie to wytrzymać.

	Nic nie jest modyfikowane.

  down
	Zatrzymuje wszystkie dyski macierzy.

	To polecenie używa narzędzia `smartctl` i jest równoważne
	uruchomieniu `smartctl -s standby,now` na wszystkich urządzeniach.

	Możesz zatrzymać tylko określone dyski za pomocą opcji -d, --filter-disk.

	Aby automatycznie zatrzymać dyski w przypadku błędu, możesz użyć
	opcji -s, --spin-down-on-error z dowolnym innym poleceniem, co jest
	równoważne ręcznemu uruchomieniu `down`, gdy wystąpi błąd.

	Nic nie jest modyfikowane.

  diff
	Wyświetla listę wszystkich plików zmodyfikowanych od ostatniej `sync`, które
	wymagają ponownego obliczenia danych parzystości.

	To polecenie nie sprawdza danych pliku, a jedynie znacznik czasu pliku,
	rozmiar i inode.

	Po wyświetleniu listy wszystkich zmienionych plików, prezentowane jest
	podsumowanie zmian, pogrupowane według:
		equal - Pliki niezmienione od wcześniej.
		added - Pliki dodane, których wcześniej nie było.
		removed - Pliki usunięte.
		updated - Pliki o innym rozmiarze lub znaczniku czasu, co oznacza, że
			zostały zmodyfikowane.
		moved - Pliki przeniesione do innego katalogu na tym samym dysku.
			Są one identyfikowane przez tę samą nazwę, rozmiar, znacznik czasu
			i inode, ale inny katalog.
		copied - Pliki skopiowane na tym samym lub innym dysku. Zauważ, że jeśli
			zostały faktycznie przeniesione na inny dysk, zostaną również
			policzone w `removed`.
			Są one identyfikowane przez tę samą nazwę, rozmiar i
			znacznik czasu. Jeśli znacznik czasu z dokładnością do podsekundy wynosi zero,
			musi pasować pełna ścieżka, a nie tylko nazwa.
		restored - Pliki o innym inode, ale pasującej nazwie, rozmiarze i znaczniku czasu.
			Są to zazwyczaj pliki przywrócone po usunięciu.

	Jeśli wymagana jest `sync`, kod powrotu procesu wynosi 2, zamiast
	domyślnego 0. Kod powrotu 1 jest używany dla ogólnego stanu błędu.

	Nic nie jest modyfikowane.

  sync
	Aktualizuje informacje o parzystości. Wszystkie zmodyfikowane pliki
	w macierzy dysków są odczytywane, a odpowiednie dane parzystości
	są aktualizowane.

	Możesz zatrzymać ten proces w dowolnym momencie, naciskając Ctrl+C,
	bez utraty już wykonanej pracy.
	Przy następnym uruchomieniu proces `sync` wznowi się w miejscu, w którym
	został przerwany.

	Jeśli podczas procesu zostaną znalezione ciche błędy lub błędy wejścia/wyjścia,
	odpowiednie bloki zostaną oznaczone jako złe.

	Pliki są identyfikowane po ścieżce i/lub inode i sprawdzane
	po rozmiarze i znaczniku czasu.
	Jeśli rozmiar pliku lub znacznik czasu się różni, dane parzystości
	są ponownie obliczane dla całego pliku.
	Jeśli plik zostanie przeniesiony lub zmieniony na tym samym dysku, zachowując ten sam inode,
	parzystość nie jest ponownie obliczana.
	Jeśli plik zostanie przeniesiony na inny dysk, parzystość jest ponownie obliczana,
	ale wcześniej obliczone informacje o haszu są zachowywane.

	Pliki `content` i `parity` są modyfikowane w razie potrzeby.
	Pliki w macierzy NIE są modyfikowane.

  scrub
	Sprawdza macierz, szukając cichych błędów lub błędów wejścia/wyjścia w danych
	i dyskach parzystości.

	Każde wywołanie sprawdza około 8% macierzy, z wyłączeniem
	danych już sprawdzonych w ciągu ostatnich 10 dni.
	Oznacza to, że sprawdzanie raz w tygodniu zapewnia sprawdzenie każdego bitu danych
	przynajmniej raz na trzy miesiące.

	Możesz zdefiniować inny plan sprawdzania lub ilość za pomocą opcji -p, --plan,
	która akceptuje:
	bad - Sprawdza bloki oznaczone jako złe.
	new - Sprawdza bloki właśnie zsynchronizowane, jeszcze niesprawdzone.
	full - Sprawdza wszystko.
	0-100 - Sprawdza określony procent bloków.

	Jeśli określisz wartość procentową, możesz również użyć opcji -o, --older-than,
	aby zdefiniować, jak stary powinien być blok.
	Najpierw sprawdzane są najstarsze bloki, co zapewnia optymalne sprawdzenie.
	Jeśli chcesz sprawdzić tylko bloki właśnie zsynchronizowane, jeszcze niesprawdzone,
	użyj opcji `-p new`.

	Aby uzyskać szczegóły statusu sprawdzania, użyj polecenia `status`.

	Dla każdego znalezionego cichego błędu lub błędu wejścia/wyjścia, odpowiednie bloki
	są oznaczane jako złe w pliku `content`.
	Te złe bloki są wymienione w `status` i mogą zostać naprawione za pomocą `fix -e`.
	Po naprawie, przy następnym sprawdzeniu, zostaną one ponownie sprawdzone, a jeśli okaże się,
	że zostały poprawione, oznaczenie złego zostanie usunięte.
	Aby sprawdzić tylko złe bloki, możesz użyć polecenia `scrub -p bad`.

	Zaleca się uruchamianie `scrub` tylko na zsynchronizowanej macierzy, aby uniknąć
	zgłaszania błędów spowodowanych przez niezsynchronizowane dane. Błędy te są rozpoznawane
	jako nie będące cichymi błędami, a bloki nie są oznaczane jako złe,
	ale takie błędy są zgłaszane w wyniku polecenia.

	Plik `content` jest modyfikowany w celu zaktualizowania czasu ostatniego sprawdzenia
	dla każdego bloku i oznaczenia złych bloków.
	Pliki `parity` NIE są modyfikowane.
	Pliki w macierzy NIE są modyfikowane.

  fix
	Naprawia wszystkie pliki i dane parzystości.

	Wszystkie pliki i dane parzystości są porównywane ze stanem migawki
	zapisanym w ostatniej `sync`.
	Jeśli zostanie znaleziona różnica, jest ona przywracana do zapisanego stanu migawki.

	OSTRZEŻENIE! Polecenie `fix` nie rozróżnia błędów od
	celowych modyfikacji. Bezwarunkowo przywraca stan pliku
	do ostatniej `sync`.

	Jeśli nie określono żadnej innej opcji, przetwarzana jest cała macierz.
	Użyj opcji filtra, aby wybrać podzbiór plików lub dysków do operacji.

	Aby naprawić tylko bloki oznaczone jako złe podczas `sync` i `scrub`,
	użyj opcji -e, --filter-error.
	W przeciwieństwie do innych opcji filtra, ta stosuje poprawki tylko do plików, które są
	niezmienione od ostatniej `sync`.

	SnapRAID zmienia nazwy wszystkich plików, których nie można naprawić, dodając
	rozszerzenie `.unrecoverable`.

	Przed naprawą, cała macierz jest skanowana w celu znalezienia wszelkich plików przeniesionych
	od ostatniej operacji `sync`.
	Pliki te są identyfikowane na podstawie ich znacznika czasu, ignorując ich nazwę
	i katalog, i są używane w procesie odzyskiwania, jeśli to konieczne.
	Jeśli przeniosłeś niektóre z nich poza macierz, możesz użyć opcji -i, --import,
	aby określić dodatkowe katalogi do skanowania.

	Pliki są identyfikowane tylko po ścieżce, a nie po inode.

	Plik `content` NIE jest modyfikowany.
	Pliki `parity` są modyfikowane w razie potrzeby.
	Pliki w macierzy są modyfikowane w razie potrzeby.

  check
	Weryfikuje wszystkie pliki i dane parzystości.

	Działa jak `fix`, ale tylko symuluje odzyskiwanie i żadne zmiany
	nie są zapisywane w macierzy.

	To polecenie jest przeznaczone głównie do ręcznej weryfikacji,
	takiej jak po procesie odzyskiwania lub w innych specjalnych warunkach.
	Do okresowych i zaplanowanych sprawdzeń użyj `scrub`.

	Jeśli użyjesz opcji -a, --audit-only, sprawdzane są tylko
	dane pliku, a dane parzystości są ignorowane, co zapewnia
	szybsze działanie.

	Pliki są identyfikowane tylko po ścieżce, a nie po inode.

	Nic nie jest modyfikowane.

  list
	Wyświetla listę wszystkich plików zawartych w macierzy w momencie
	ostatniej `sync`.

	Z -v lub --verbose, wyświetlany jest również czas z dokładnością do podsekundy.

	Nic nie jest modyfikowane.

  dup
	Wyświetla listę wszystkich zduplikowanych plików. Dwa pliki są uważane za równe, jeśli ich
	hasze pasują. Dane pliku nie są odczytywane; używane są tylko
	wcześniej obliczone hasze.

	Nic nie jest modyfikowane.

  pool
	Tworzy lub aktualizuje wirtualny widok wszystkich
	plików w Twojej macierzy dysków w katalogu `pooling`.

	Pliki nie są kopiowane, ale linkowane za pomocą
	dowiązań symbolicznych.

	Podczas aktualizacji, wszystkie istniejące dowiązania symboliczne i puste
	podkatalogi są usuwane i zastępowane nowym
	widokiem macierzy. Wszelkie inne zwykłe pliki pozostają na swoim miejscu.

	Nic nie jest modyfikowane poza katalogiem puli.

  devices
	Wyświetla urządzenia niskiego poziomu używane przez macierz.

	To polecenie wyświetla skojarzenia urządzeń w macierzy
	i jest przeznaczone głównie jako interfejs skryptowy.

	Pierwsze dwie kolumny to identyfikator i ścieżka urządzenia niskiego poziomu.
	Następne dwie kolumny to identyfikator i ścieżka urządzenia wysokiego poziomu.
	Ostatnia kolumna to nazwa dysku w macierzy.

	W większości przypadków masz jedno urządzenie niskiego poziomu dla każdego dysku w
	macierzy, ale w niektórych bardziej złożonych konfiguracjach możesz mieć wiele
	urządzeń niskiego poziomu używanych przez pojedynczy dysk w macierzy.

	Nic nie jest modyfikowane.

  touch
	Ustawia arbitralny znacznik czasu z dokładnością do podsekundy dla wszystkich plików,
	które mają go ustawiony na zero.

	Poprawia to zdolność SnapRAID do rozpoznawania przeniesionych
	i skopiowanych plików, ponieważ sprawia, że znacznik czasu jest prawie unikalny,
	redukując możliwe duplikaty.

	Dokładniej, jeśli znacznik czasu z dokładnością do podsekundy nie jest zerowy,
	przeniesiony lub skopiowany plik jest identyfikowany jako taki, jeśli pasuje do
	nazwy, rozmiaru i znacznika czasu. Jeśli znacznik czasu z dokładnością do podsekundy
	jest zerowy, jest uważany za kopię tylko wtedy, gdy pasują pełna ścieżka,
	rozmiar i znacznik czasu.

	Znacznik czasu z dokładnością do sekundy nie jest modyfikowany,
	więc wszystkie daty i czasy Twoich plików zostaną zachowane.

  rehash
	Planuje ponowne haszowanie całej macierzy.

	To polecenie zmienia używany rodzaj hasza, zazwyczaj podczas aktualizacji
	z systemu 32-bitowego na 64-bitowy, aby przełączyć się z
	MurmurHash3 na szybszy SpookyHash.

	Jeśli używasz już optymalnego hasza, to polecenie
	nic nie robi i informuje Cię, że nie jest potrzebne żadne działanie.

	Ponowne haszowanie nie jest wykonywane natychmiast, ale odbywa się
	stopniowo podczas `sync` i `scrub`.

	Możesz sprawdzić stan ponownego haszowania za pomocą `status`.

	Podczas ponownego haszowania SnapRAID zachowuje pełną funkcjonalność,
	z jedynym wyjątkiem, że `dup` nie może wykryć zduplikowanych
	plików używających innego hasza.

Options
	SnapRAID udostępnia następujące opcje:

	-c, --conf CONFIG
		Wybiera plik konfiguracyjny do użycia. Jeśli nie określono, w systemie Unix
		używa pliku `/usr/local/etc/snapraid.conf`, jeśli istnieje,
		w przeciwnym razie `/etc/snapraid.conf`.
		W systemie Windows używa pliku `snapraid.conf` w tym samym
		katalogu co `snapraid.exe`.

	-f, --filter PATTERN
		Filtruje pliki do przetworzenia w `check` i `fix`.
		Przetwarzane są tylko pliki pasujące do określonego wzorca.
		Ta opcja może być używana wielokrotnie.
		Zobacz sekcję PATTERN, aby uzyskać więcej szczegółów na temat
		specyfikacji wzorców.
		W systemie Unix upewnij się, że znaki globbing są cytowane, jeśli są używane.
		Ta opcja może być używana tylko z `check` i `fix`.
		Nie może być używana z `sync` i `scrub`, ponieważ zawsze
		przetwarzają całą macierz.

	-d, --filter-disk NAME
		Filtruje dyski do przetworzenia w `check`, `fix`, `up` i `down`.
		Musisz określić nazwę dysku zdefiniowaną w pliku konfiguracyjnym.
		Możesz również określić dyski parzystości za pomocą nazw: `parity`, `2-parity`,
		`3-parity`, itp., aby ograniczyć operacje do określonego dysku parzystości.
		Jeśli połączysz wiele opcji --filter, --filter-disk i --filter-missing,
		wybrane zostaną tylko pliki pasujące do wszystkich filtrów.
		Ta opcja może być używana wielokrotnie.
		Ta opcja może być używana tylko z `check`, `fix`, `up` i `down`.
		Nie może być używana z `sync` i `scrub`, ponieważ zawsze
		przetwarzają całą macierz.

	-m, --filter-missing
		Filtruje pliki do przetworzenia w `check` i `fix`.
		Przetwarzane są tylko pliki brakujące lub usunięte z macierzy.
		Użyte z `fix`, działa to jako polecenie `undelete`.
		Jeśli połączysz wiele opcji --filter, --filter-disk i --filter-missing,
		wybrane zostaną tylko pliki pasujące do wszystkich filtrów.
		Ta opcja może być używana tylko z `check` i `fix`.
		Nie może być używana z `sync` i `scrub`, ponieważ zawsze
		przetwarzają całą macierz.

	-e, --filter-error
		Przetwarza pliki z błędami w `check` i `fix`.
		Przetwarza tylko pliki, które mają bloki oznaczone cichymi
		błędami lub błędami wejścia/wyjścia podczas `sync` i `scrub`, jak wymieniono w `status`.
		Ta opcja może być używana tylko z `check` i `fix`.

	-p, --plan PERC|bad|new|full
		Wybiera plan sprawdzania. Jeśli PERC jest wartością numeryczną od 0 do 100,
		jest interpretowany jako procent bloków do sprawdzenia.
		Zamiast wartości procentowej możesz określić plan:
		`bad` sprawdza złe bloki, `new` sprawdza bloki jeszcze niesprawdzone,
		a `full` sprawdza wszystko.
		Ta opcja może być używana tylko z `scrub`.

	-o, --older-than DAYS
		Wybiera najstarszą część macierzy do przetworzenia w `scrub`.
		DAYS to minimalny wiek w dniach dla bloku, który ma być sprawdzony;
		domyślnie jest to 10.
		Bloki oznaczone jako złe są zawsze sprawdzane niezależnie od tej opcji.
		Ta opcja może być używana tylko z `scrub`.

	-a, --audit-only
		W `check`, weryfikuje hasz plików bez
		sprawdzania danych parzystości.
		Jeśli interesuje Cię tylko sprawdzenie danych pliku, ta
		opcja może znacznie przyspieszyć proces sprawdzania.
		Ta opcja może być używana tylko z `check`.

	-h, --pre-hash
		W `sync`, uruchamia wstępną fazę haszowania wszystkich nowych danych
		w celu dodatkowej weryfikacji przed obliczeniem parzystości.
		Zazwyczaj w `sync` nie jest wykonywane wstępne haszowanie, a nowe
		dane są haszowane tuż przed obliczeniem parzystości, gdy są odczytywane
		po raz pierwszy.
		Ten proces zachodzi, gdy system jest pod
		dużym obciążeniem, wszystkie dyski się kręcą, a CPU jest zajęte.
		Jest to ekstremalny warunek dla maszyny, a jeśli ma
		ukryty problem sprzętowy, ciche błędy mogą pozostać niewykryte,
		ponieważ dane nie są jeszcze haszowane.
		Aby uniknąć tego ryzyka, możesz włączyć tryb `pre-hash`, aby
		wszystkie dane były odczytywane dwukrotnie w celu zapewnienia ich integralności.
		Ta opcja weryfikuje również pliki przeniesione wewnątrz macierzy,
		aby upewnić się, że operacja przeniesienia zakończyła się pomyślnie, i, jeśli to konieczne,
		umożliwia uruchomienie operacji fix przed kontynuowaniem.
		Ta opcja może być używana tylko z `sync`.

	-i, --import DIR
		Importuje z określonego katalogu wszelkie pliki usunięte
		z macierzy po ostatniej `sync`.
		Jeśli nadal masz takie pliki, mogą być użyte przez `check`
		i `fix`, aby poprawić proces odzyskiwania.
		Pliki są odczytywane, w tym w podkatalogach, i są
		identyfikowane niezależnie od ich nazwy.
		Ta opcja może być używana tylko z `check` i `fix`.

	-s, --spin-down-on-error
		W przypadku każdego błędu, zatrzymuje wszystkie zarządzane dyski przed wyjściem
		z niezerowym kodem statusu. Zapobiega to pozostawaniu dysków
		aktywnymi i kręcącymi się po przerwanej operacji,
		pomagając uniknąć niepotrzebnego gromadzenia się ciepła i zużycia
		energii. Użyj tej opcji, aby upewnić się, że dyski są bezpiecznie
		zatrzymane, nawet gdy polecenie zakończy się niepowodzeniem.

	-w, --bw-limit RATE
		Stosuje globalne ograniczenie przepustowości dla wszystkich dysków. RATE to
		liczba bajtów na sekundę. Możesz określić mnożnik,
		taki jak K, M lub G (np. --bw-limit 1G).

	-A, --stats
		Włącza rozszerzony widok statusu, który pokazuje dodatkowe informacje.
		Ekran wyświetla dwa wykresy:
		Pierwszy wykres pokazuje liczbę buforowanych pasków dla każdego
		dysku, wraz ze ścieżką pliku, do którego aktualnie uzyskuje się dostęp
		na tym dysku. Zazwyczaj najwolniejszy dysk nie będzie miał dostępnego
		bufora, co determinuje maksymalną osiągalną przepustowość.
		Drugi wykres pokazuje procent czasu spędzonego na czekaniu
		w ciągu ostatnich 100 sekund. Oczekuje się, że najwolniejszy dysk
		spowoduje większość czasu oczekiwania, podczas gdy inne dyski powinny mieć
		niewielki lub żaden czas oczekiwania, ponieważ mogą używać swoich buforowanych pasków.
		Ten wykres pokazuje również czas spędzony na czekaniu na obliczenia
		hasza i obliczenia RAID.
		Wszystkie obliczenia są wykonywane równolegle z operacjami dyskowymi.
		Dlatego, dopóki istnieje mierzalny czas oczekiwania dla
		przynajmniej jednego dysku, oznacza to, że CPU jest wystarczająco szybkie, aby
		nadążyć za obciążeniem.

	-Z, --force-zero
		Wymusza niebezpieczną operację synchronizacji pliku o zerowym
		rozmiarze, który wcześniej był niezerowy.
		Jeśli SnapRAID wykryje taki stan, zatrzymuje przetwarzanie,
		chyba że określisz tę opcję.
		Pozwala to łatwo wykryć, kiedy po awarii systemu
		niektóre dostępne pliki zostały obcięte.
		Jest to możliwy stan w systemie Linux z systemami plików ext3/ext4.
		Ta opcja może być używana tylko z `sync`.

	-E, --force-empty
		Wymusza niebezpieczną operację synchronizacji dysku z brakującymi
		wszystkimi oryginalnymi plikami.
		Jeśli SnapRAID wykryje, że brakuje wszystkich plików pierwotnie obecnych
		na dysku lub zostały przepisane, zatrzymuje przetwarzanie,
		chyba że określisz tę opcję.
		Pozwala to łatwo wykryć, kiedy system plików danych nie jest
		zamontowany.
		Ta opcja może być używana tylko z `sync`.

	-U, --force-uuid
		Wymusza niebezpieczną operację synchronizacji, sprawdzania i naprawiania
		z dyskami, które zmieniły swój UUID.
		Jeśli SnapRAID wykryje, że niektóre dyski zmieniły UUID,
		zatrzymuje przetwarzanie, chyba że określisz tę opcję.
		Pozwala to wykryć, kiedy Twoje dyski są zamontowane w
		niewłaściwych punktach montowania.
		Dozwolona jest jednak pojedyncza zmiana UUID z
		pojedynczą parzystością i więcej z wielokrotną parzystością, ponieważ jest to
		normalny przypadek podczas wymiany dysków po odzyskaniu.
		Ta opcja może być używana tylko z `sync`, `check` lub
		`fix`.

	-D, --force-device
		Wymusza niebezpieczną operację naprawiania z niedostępnymi dyskami
		lub z dyskami na tym samym urządzeniu fizycznym.
		Na przykład, jeśli straciłeś dwa dyski danych i masz zapasowy dysk do odzyskania
		tylko pierwszego, możesz zignorować drugi niedostępny dysk.
		Lub, jeśli chcesz odzyskać dysk w wolnym miejscu pozostawionym na
		już używanym dysku, współdzielącym to samo urządzenie fizyczne.
		Ta opcja może być używana tylko z `fix`.

	-N, --force-nocopy
		W `sync`, `check` i `fix`, wyłącza heurystykę wykrywania kopii.
		Bez tej opcji SnapRAID zakłada, że pliki o tych samych
		atrybutach, takich jak nazwa, rozmiar i znacznik czasu, są kopiami z tymi
		samymi danymi.
		Pozwala to na identyfikację skopiowanych lub przeniesionych plików z jednego dysku
		na inny i ponowne wykorzystanie już obliczonych informacji o haszu
		w celu wykrycia cichych błędów lub odzyskania brakujących plików.
		W niektórych rzadkich przypadkach to zachowanie może prowadzić do fałszywych alarmów
		lub spowolnienia procesu z powodu wielu weryfikacji hasza, a ta
		opcja pozwala rozwiązać takie problemy.
		Ta opcja może być używana tylko z `sync`, `check` i `fix`.

	-F, --force-full
		W `sync`, wymusza pełne ponowne obliczenie parzystości.
		Ta opcja może być używana, gdy dodajesz nowy poziom parzystości lub jeśli
		powróciłeś do starego pliku zawartości, używając nowszych danych parzystości.
		Zamiast ponownego tworzenia parzystości od podstaw, pozwala to
		na ponowne wykorzystanie haszy obecnych w pliku zawartości do walidacji danych
		i utrzymania ochrony danych podczas procesu `sync` za pomocą
		istniejących danych parzystości.
		Ta opcja może być używana tylko z `sync`.

	-R, --force-realloc
		W `sync`, wymusza pełną realokację plików i przebudowę parzystości.
		Ta opcja może być używana do całkowitej realokacji wszystkich plików,
		usuwając fragmentację, jednocześnie ponownie wykorzystując hasze obecne w pliku zawartości
		do walidacji danych.
		Ta opcja może być używana tylko z `sync`.
		OSTRZEŻENIE! Ta opcja jest tylko dla ekspertów i jest wysoce
		zalecane, aby jej nie używać.
		NIE MASZ ochrony danych podczas operacji `sync`.

	-l, --log FILE
		Zapisuje szczegółowy log do określonego pliku.
		Jeśli ta opcja nie jest określona, nieoczekiwane błędy są drukowane
		na ekranie, potencjalnie prowadząc do nadmiernego wyjścia w przypadku
		wielu błędów. Gdy -l, --log jest określone, tylko
		krytyczne błędy, które powodują zatrzymanie SnapRAID, są drukowane
		na ekranie.
		Jeśli ścieżka zaczyna się od `>>`, plik jest otwierany
		w trybie dołączania. Wystąpienia `%D` i `%T` w nazwie są
		zastępowane datą i czasem w formacie RRRRMMDD i
		GGMMSS. W plikach wsadowych Windows musisz podwoić
		znak `%`, np. result-%%D.log. Aby użyć `>>`, musisz
		ujęć nazwę w cudzysłów, np. `">>result.log"`.
		Aby wyprowadzić log do standardowego wyjścia lub standardowego błędu,
		możesz użyć odpowiednio `">&1"` i `">&2"`.
		Opisy tagów logu znajdziesz w pliku snapraid_log.txt lub na stronie podręcznika.

	-L, --error-limit NUMBER
		Ustawia nowy limit błędów przed zatrzymaniem wykonania.
		Domyślnie SnapRAID zatrzymuje się, jeśli napotka więcej niż 100
		błędów wejścia/wyjścia, co wskazuje, że dysk prawdopodobnie ulega awarii.
		Ta opcja dotyczy `sync` i `scrub`, które mogą
		kontynuować po pierwszym zestawie błędów dysku, aby spróbować
		ukończyć swoje operacje.
		Jednak `check` i `fix` zawsze zatrzymują się przy pierwszym błędzie.

	-S, --start BLKSTART
		Zaczyna przetwarzanie od określonego
		numeru bloku. Może to być przydatne do ponownej próby sprawdzenia
		lub naprawy określonych bloków w przypadku uszkodzonego dysku.
		Ta opcja jest przeznaczona głównie do zaawansowanego ręcznego odzyskiwania.

	-B, --count BLKCOUNT
		Przetwarza tylko określoną liczbę bloków.
		Ta opcja jest przeznaczona głównie do zaawansowanego ręcznego odzyskiwania.

	-C, --gen-conf CONTENT
		Generuje atrapę pliku konfiguracyjnego z istniejącego
		pliku zawartości.
		Plik konfiguracyjny jest zapisywany na standardowe wyjście
		i nie nadpisuje istniejącego.
		Ten plik konfiguracyjny zawiera również informacje
		potrzebne do rekonstrukcji punktów montowania dysków na wypadek
		utraty całego systemu.

	-v, --verbose
		Wyświetla więcej informacji na ekranie.
		Jeśli określono raz, drukuje wykluczone pliki
		i dodatkowe statystyki.
		Ta opcja nie ma wpływu na pliki logów.

	-q, --quiet
		Wyświetla mniej informacji na ekranie.
		Jeśli określono raz, usuwa pasek postępu; dwa razy,
		bieżące operacje; trzy razy, komunikaty informacyjne; cztery razy,
		komunikaty statusu.
		Krytyczne błędy są zawsze drukowane na ekranie.
		Ta opcja nie ma wpływu na pliki logów.

	-H, --help
		Wyświetla krótki ekran pomocy.

	-V, --version
		Wyświetla wersję programu.

Configuration
	SnapRAID wymaga pliku konfiguracyjnego, aby wiedzieć, gdzie znajduje się Twoja macierz
	dysków i gdzie przechowywać informacje o parzystości.

	W systemie Unix używa pliku `/usr/local/etc/snapraid.conf`, jeśli istnieje,
	w przeciwnym razie `/etc/snapraid.conf`.
	W systemie Windows używa pliku `snapraid.conf` w tym samym
	katalogu co `snapraid.exe`.

	Musi zawierać następujące opcje (uwzględniające wielkość liter):

  parity FILE [,FILE] ...
	Definiuje pliki do użycia do przechowywania informacji o parzystości.
	Parzystość umożliwia ochronę przed awarią pojedynczego dysku,
	podobnie jak w RAID5.

	Możesz określić wiele plików, które muszą znajdować się na różnych dyskach.
	Gdy plik nie może już rosnąć, używany jest następny.
	Całkowita dostępna przestrzeń musi być co najmniej tak duża jak największy dysk danych w
	macierzy.

	Możesz dodać dodatkowe pliki parzystości później, ale nie możesz
	ich zmieniać kolejności ani usuwać.

	Trzymanie dysków parzystości zarezerwowanych dla parzystości zapewnia, że
	nie ulegną fragmentacji, co poprawia wydajność.

	W systemie Windows 256 MB jest pozostawione niewykorzystane na każdym dysku, aby uniknąć
	ostrzeżenia o pełnych dyskach.

	Ta opcja jest obowiązkowa i może być używana tylko raz.

  (2,3,4,5,6)-parity FILE [,FILE] ...
	Definiuje pliki do użycia do przechowywania dodatkowych informacji o parzystości.

	Dla każdego określonego poziomu parzystości włączany jest jeden dodatkowy
	poziom ochrony:

	* 2-parity włącza podwójną parzystość RAID6.
	* 3-parity włącza potrójną parzystość.
	* 4-parity włącza poczwórną (cztery) parzystość.
	* 5-parity włącza pięciokrotną (pięć) parzystość.
	* 6-parity włącza sześciokrotną (sześć) parzystość.

	Każdy poziom parzystości wymaga obecności wszystkich poprzednich poziomów parzystości.

	Obowiązują te same uwagi, co dla opcji `parity`.

	Te opcje są opcjonalne i mogą być używane tylko raz.

  z-parity FILE [,FILE] ...
	Definiuje alternatywny plik i format do przechowywania potrójnej parzystości.

	Ta opcja jest alternatywą dla `3-parity`, przeznaczoną głównie dla
	procesorów niskiej klasy, takich jak ARM lub AMD Phenom, Athlon i Opteron,
	które nie obsługują zestawu instrukcji SSSE3. W takich przypadkach zapewnia
	lepszą wydajność.

	Ten format jest podobny, ale szybszy niż ten używany przez ZFS RAIDZ3.
	Podobnie jak ZFS, nie działa powyżej potrójnej parzystości.

	Podczas używania `3-parity` zostaniesz ostrzeżony, jeśli zaleca się użycie
	formatu `z-parity` w celu poprawy wydajności.

	Możliwa jest konwersja z jednego formatu na inny poprzez dostosowanie
	pliku konfiguracyjnego z pożądanym plikiem z-parity lub 3-parity
	i użycie `fix` do jego ponownego utworzenia.

  content FILE
	Definiuje plik do użycia do przechowywania listy i sum kontrolnych wszystkich
	plików obecnych w Twojej macierzy dysków.

	Może być umieszczony na dysku używanym do danych, parzystości lub
	każdym innym dostępnym dysku.
	Jeśli użyjesz dysku danych, ten plik jest automatycznie wykluczony
	z procesu `sync`.

	Ta opcja jest obowiązkowa i może być używana wielokrotnie do zapisania
	wielu kopii tego samego pliku.

	Musisz przechowywać co najmniej jedną kopię dla każdego używanego dysku parzystości
	plus jeden. Używanie dodatkowych kopii nie szkodzi.

  data NAME DIR
	Definiuje nazwę i punkt montowania dysków danych w
	macierzy. NAME służy do identyfikacji dysku i musi
	być unikalna. DIR to punkt montowania dysku w
	systemie plików.

	Możesz zmieniać punkt montowania w razie potrzeby, o ile
	zachowasz stałą nazwę NAME.

	Powinieneś użyć jednej opcji dla każdego dysku danych w macierzy.

	Możesz później zmienić nazwę dysku, zmieniając bezpośrednio nazwę NAME
	w pliku konfiguracyjnym, a następnie uruchamiając polecenie `sync`.
	W przypadku zmiany nazwy, skojarzenie odbywa się za pomocą zapisanego
	UUID dysków.

  nohidden
	Wyklucza wszystkie ukryte pliki i katalogi.
	W systemie Unix ukryte pliki to te zaczynające się od `.`.
	W systemie Windows są to te z atrybutem ukrycia.

  exclude/include PATTERN
	Definiuje wzorce plików lub katalogów do wykluczenia lub włączenia
	do procesu synchronizacji.
	Wszystkie wzorce są przetwarzane w określonej kolejności.

	Jeśli pierwszy pasujący wzorzec jest `exclude`, plik
	jest wykluczony. Jeśli jest to `include`, plik jest włączony.
	Jeśli żaden wzorzec nie pasuje, plik jest wykluczony, jeśli ostatni określony
	wzorzec to `include`, lub włączony, jeśli ostatni określony wzorzec to `exclude`.

	Zobacz sekcję PATTERN, aby uzyskać więcej szczegółów na temat specyfikacji
	wzorców.

	Ta opcja może być używana wielokrotnie.

  blocksize SIZE_IN_KIBIBYTES
	Definiuje podstawowy rozmiar bloku w kibibajtach dla parzystości.
	Jeden kibibajt to 1024 bajty.

	Domyślny rozmiar bloku to 256, co powinno działać w większości przypadków.

	OSTRZEŻENIE! Ta opcja jest tylko dla ekspertów i jest wysoce
	zalecane, aby nie zmieniać tej wartości. Aby zmienić tę wartość w przyszłości,
	będziesz musiał ponownie utworzyć całą parzystość!

	Powodem użycia innego rozmiaru bloku jest posiadanie wielu małych
	plików, rzędu milionów.

	Dla każdego pliku, nawet jeśli ma tylko kilka bajtów, alokowany jest cały blok parzystości,
	a przy wielu plikach może to skutkować znaczną nieużywaną przestrzenią parzystości.
	Kiedy całkowicie wypełnisz dysk parzystości, nie możesz
	dodawać więcej plików do dysków danych.
	Jednak marnowana parzystość nie gromadzi się na dyskach danych. Marnowane miejsce
	wynikające z dużej liczby plików na dysku danych ogranicza tylko
	ilość danych na tym dysku danych, a nie na innych.

	Jako przybliżenie, możesz założyć, że połowa rozmiaru bloku jest
	marnowana dla każdego pliku. Na przykład, z 100 000 plików i rozmiarem bloku 256 KiB,
	zmarnujesz 12,8 GB parzystości, co może skutkować
	12,8 GB mniejszą dostępną przestrzenią na dysku danych.

	Możesz sprawdzić ilość zmarnowanego miejsca na każdym dysku za pomocą `status`.
	Jest to ilość miejsca, którą musisz pozostawić wolną na dyskach danych
	lub użyć dla plików nie włączonych do macierzy.
	Jeśli ta wartość jest ujemna, oznacza to, że jesteś blisko wypełnienia
	parzystości i reprezentuje to przestrzeń, którą nadal możesz zmarnować.

	Aby uniknąć tego problemu, możesz użyć większej partycji dla parzystości.
	Na przykład, jeśli partycja parzystości jest o 12,8 GB większa niż dyski danych,
	masz wystarczająco dużo dodatkowego miejsca, aby obsłużyć do 100 000
	plików na każdym dysku danych bez marnowania miejsca.

	Sztuczką, aby uzyskać większą partycję parzystości w systemie Linux, jest sformatowanie jej
	za pomocą polecenia:

		:mkfs.ext4 -m 0 -T largefile4 DEVICE

	Daje to około 1,5% dodatkowego miejsca, około 60 GB dla
	dysku 4 TB, co pozwala na około 460 000 plików na każdym dysku danych bez
	marnowania miejsca.

  hashsize SIZE_IN_BYTES
	Definiuje rozmiar hasza w bajtach dla zapisanych bloków.

	Domyślny rozmiar hasza to 16 bajtów (128 bitów), co powinno działać
	w większości przypadków.

	OSTRZEŻENIE! Ta opcja jest tylko dla ekspertów i jest wysoce
	zalecane, aby nie zmieniać tej wartości. Aby zmienić tę wartość w przyszłości,
	będziesz musiał ponownie utworzyć całą parzystość!

	Powodem użycia innego rozmiaru hasza jest to, że Twój system ma
	ograniczoną pamięć. Z reguły, SnapRAID zwykle wymaga
	1 GiB pamięci RAM na każde 16 TB danych w macierzy.

	Konkretnie, aby przechowywać hasze danych, SnapRAID wymaga
	około TS*(1+HS)/BS bajtów pamięci RAM,
	gdzie TS to całkowity rozmiar w bajtach Twojej macierzy dysków, BS to
	rozmiar bloku w bajtach, a HS to rozmiar hasza w bajtach.

	Na przykład, z 8 dyskami po 4 TB, rozmiarem bloku 256 KiB
	(1 KiB = 1024 bajty) i rozmiarem hasza 16, otrzymujesz:

	:RAM = (8 * 4 * 10^12) * (1+16) / (256 * 2^10) = 1,93 GiB

	Przełączając się na rozmiar hasza 8, otrzymujesz:

	:RAM = (8 * 4 * 10^12) * (1+8) / (256 * 2^10) = 1,02 GiB

	Przełączając się na rozmiar bloku 512, otrzymujesz:

	:RAM = (8 * 4 * 10^12) * (1+16) / (512 * 2^10) = 0,96 GiB

	Przełączając się zarówno na rozmiar hasza 8, jak i rozmiar bloku 512, otrzymujesz:

	:RAM = (8 * 4 * 10^12) * (1+8) / (512 * 2^10) = 0,51 GiB

  autosave SIZE_IN_GIGABYTES
	Automatycznie zapisuje stan podczas synchronizacji lub sprawdzania po przetworzeniu
	określonej ilości GB.
	Ta opcja jest przydatna, aby uniknąć ponownego uruchamiania długich poleceń `sync`
	od zera, jeśli zostaną przerwane przez awarię maszyny lub inne zdarzenie.

  temp_limit TEMPERATURE_CELSIUS
	Ustawia maksymalną dozwoloną temperaturę dysku w stopniach Celsjusza. Po określeniu,
	SnapRAID okresowo sprawdza temperaturę wszystkich dysków za pomocą
	narzędzia smartctl. Aktualne temperatury dysków są wyświetlane podczas
	działania SnapRAID. Jeśli którykolwiek dysk przekroczy ten limit, wszystkie operacje
	zostają zatrzymane, a dyski są zatrzymywane (przechodzą w stan gotowości) na czas
	zdefiniowany przez opcję `temp_sleep`. Po okresie uśpienia, operacje
	są wznawiane, potencjalnie ponownie zatrzymując się, jeśli limit temperatury
	zostanie ponownie osiągnięty.

	Podczas działania SnapRAID analizuje również krzywą nagrzewania każdego
	dysku i szacuje długoterminową stałą temperaturę, którą mają
	osiągnąć, jeśli aktywność będzie kontynuowana. Oszacowanie jest wykonywane tylko po
	czterokrotnym wzroście temperatury dysku, co zapewnia, że
	dostępnych jest wystarczająco dużo punktów danych do ustalenia wiarygodnego trendu.
	Ta przewidywana stała temperatura jest pokazywana w nawiasach obok
	aktualnej wartości i pomaga ocenić, czy chłodzenie systemu jest
	odpowiednie. Ta szacowana temperatura ma charakter wyłącznie informacyjny
	i nie ma wpływu na zachowanie SnapRAID. Działania programu
	opierają się wyłącznie na faktycznie zmierzonych temperaturach dysków.

	Aby wykonać tę analizę, SnapRAID potrzebuje odniesienia dla temperatury systemu.
	Najpierw próbuje odczytać ją z dostępnych czujników sprzętowych.
	Jeśli nie można uzyskać dostępu do żadnego czujnika systemu, jako awaryjne odniesienie
	wykorzystuje najniższą temperaturę dysku zmierzoną na początku uruchomienia.

	Normalnie SnapRAID pokazuje tylko temperaturę najgorętszego dysku.
	Aby wyświetlić temperaturę wszystkich dysków, użyj opcji -A lub --stats.

  temp_sleep TIME_IN_MINUTES
	Ustawia czas gotowości, w minutach, po osiągnięciu limitu temperatury.
	W tym okresie dyski pozostają zatrzymane. Domyślnie jest to 5 minut.

  pool DIR
	Definiuje katalog puli, w którym tworzony jest wirtualny widok macierzy
	dysków za pomocą polecenia `pool`.

	Katalog musi już istnieć.

  share UNC_DIR
	Definiuje ścieżkę UNC Windows wymaganą do zdalnego dostępu do dysków.

	Jeśli ta opcja jest określona, dowiązania symboliczne utworzone w katalogu puli
	używają tej ścieżki UNC do dostępu do dysków.
	Bez tej opcji generowane dowiązania symboliczne używają tylko ścieżek lokalnych,
	co nie pozwala na udostępnianie katalogu puli w sieci.

	Dowiązania symboliczne są tworzone przy użyciu określonej ścieżki UNC,
	dodając nazwę dysku określoną w opcji `data`, a na końcu dodając
	katalog i nazwę pliku.

	Ta opcja jest wymagana tylko dla systemu Windows.

  smartctl DISK/PARITY OPTIONS...
	Definiuje niestandardowe opcje smartctl do uzyskania atrybutów SMART dla
	każdego dysku. Może to być wymagane dla kontrolerów RAID i niektórych dysków USB,
	których nie można wykryć automatycznie. Symbol zastępczy %s jest zastępowany
	nazwą urządzenia, ale jest opcjonalny dla stałych urządzeń, takich jak kontrolery RAID.

	DISK to ta sama nazwa dysku określona w opcji `data`.
	PARITY to jedna z nazw parzystości: `parity`, `2-parity`, `3-parity`,
	`4-parity`, `5-parity`, `6-parity` lub `z-parity`.

	W określonych OPCJACH ciąg `%s` jest zastępowany
	nazwą urządzenia. W przypadku kontrolerów RAID, urządzenie jest
	prawdopodobnie stałe i może nie być konieczne użycie `%s`.

	Zapoznaj się z dokumentacją smartmontools, aby uzyskać możliwe opcje:

		:https://www.smartmontools.org/wiki/Supported_RAID-Controllers
		:https://www.smartmontools.org/wiki/Supported_USB-Devices

	Na przykład:

		:smartctl parity -d sat %s

  smartignore DISK/PARITY ATTR [ATTR...]
	Ignoruje określony atrybut SMART podczas obliczania prawdopodobieństwa
	awarii dysku. Ta opcja jest przydatna, jeśli dysk zgłasza nietypowe lub
	wprowadzające w błąd wartości dla określonego atrybutu.

	DISK to ta sama nazwa dysku określona w opcji `data`.
	PARITY to jedna z nazw parzystości: `parity`, `2-parity`, `3-parity`,
	`4-parity`, `5-parity`, `6-parity` lub `z-parity`.
	Specjalna wartość * może być użyta do zignorowania atrybutu na wszystkich dyskach.

	Na przykład, aby zignorować atrybut `Current Pending Sector Count` na
	wszystkich dyskach:

		:smartignore * 197

	Aby zignorować go tylko na pierwszym dysku parzystości:

		:smartignore parity 197

  Examples
	Przykład typowej konfiguracji dla systemu Unix:

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

	Przykład typowej konfiguracji dla systemu Windows:

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
	Wzorce zapewniają elastyczny sposób filtrowania plików do uwzględnienia lub
	wykluczenia. Używając znaków globbingowych, można zdefiniować reguły, które
	pasują do określonych nazw plików lub całych struktur katalogów bez
	ręcznego wymieniania każdej ścieżki.

	Znak zapytania `?` dopasowuje dowolny pojedynczy znak z wyjątkiem
	separatora katalogów. Dzięki temu jest przydatny do dopasowywania nazw plików ze
	zmiennymi znakami, przy jednoczesnym ograniczeniu wzorca do jednego poziomu katalogu.

	Pojedyncza gwiazdka `*` dopasowuje dowolną sekwencję znaków, ale podobnie jak
	znak zapytania, nigdy nie przekracza granic katalogów. Zatrzymuje się na
	ukośniku, co czyni ją odpowiednią do dopasowywania w obrębie jednego
	składnika ścieżki. Jest to standardowe zachowanie symboli wieloznacznych znane
	z globbingu powłoki (shell).

	Podwójna gwiazdka `**` jest potężniejsza, dopasowuje dowolną sekwencję
	znaków, w tym separatory katalogów. Pozwala to wzorcom dopasowywać się
	na wielu poziomach katalogów. Gdy `**` pojawia się bezpośrednio wewnątrz
	wzorca, może dopasować zero lub więcej znaków, w tym ukośniki między
	otaczającym tekstem dosłownym.

	Najważniejszym zastosowaniem `**` jest specjalna forma `/**/`. Dopasowuje ona
	zero lub więcej pełnych poziomów katalogów, umożliwiając dopasowanie plików
	na dowolnej głębokości w drzewie katalogów bez znajomości dokładnej struktury ścieżki.
	Na przykład wzorzec `src/**/main.js` pasuje do `src/main.js` (pomijając
	zero katalogów), `src/ui/main.js` (pomijając jeden katalog) oraz
	`src/ui/components/main.js` (pomijając dwa katalogi).

	Klasy znaków używające nawiasów kwadratowych `[]` dopasowują pojedynczy znak z
	określonego zestawu lub zakresu. Podobnie jak inne wzorce pojedynczych znaków,
	nie pasują one do separatorów katalogów. Klasy obsługują zakresy i negację za pomocą
	wykrzyknika.

	Podstawową różnicą, o której należy pamiętać, jest to, że `*`, `?` i klasy znaków
	wszystkie respektują granice katalogów i dopasowują się tylko w obrębie jednego
	składnika ścieżki, podczas gdy `**` jest jedynym wzorcem, który może dopasowywać się poprzez
	separatory katalogów.

	Istnieją cztery różne typy wzorców:

	=FILE
		Wybiera dowolny plik o nazwie FILE.
		Ten wzorzec dotyczy tylko plików, a nie katalogów.

	=DIR/
		Wybiera dowolny katalog o nazwie DIR i wszystko, co się w nim znajduje.
		Ten wzorzec dotyczy tylko katalogów, a nie plików.

	=/PATH/FILE
		Wybiera dokładnie określoną ścieżkę pliku. Ten wzorzec dotyczy
		tylko plików, a nie katalogów.

	=/PATH/DIR/
		Wybiera dokładnie określoną ścieżkę katalogu i wszystko, co się w nim
		znajduje. Ten wzorzec dotyczy tylko katalogów, a nie plików.

	Gdy określisz ścieżkę bezwzględną zaczynającą się od /, jest ona stosowana w
	katalogu głównym macierzy, a nie w katalogu głównym lokalnego systemu plików.

	W systemie Windows możesz użyć ukośnika odwrotnego \ zamiast ukośnika /.
	Katalogi systemowe Windows, połączenia, punkty montowania i inne
	specjalne katalogi Windows są traktowane jako pliki, co oznacza, że aby je
	wykluczyć, musisz użyć reguły pliku, a nie katalogu.

	Jeśli nazwa pliku zawiera znak `*`, `?`, `[`,
	lub `]`, musisz go uciec, aby uniknąć interpretacji jako
	znaku globbing. W systemie Unix znakiem ucieczki jest `\`; w systemie Windows jest to `^`.
	Gdy wzorzec znajduje się w wierszu poleceń, musisz podwoić znak ucieczki,
	aby uniknąć interpretacji przez powłokę poleceń.

	W pliku konfiguracyjnym możesz użyć różnych strategii do filtrowania
	plików do przetworzenia.
	Najprostszym podejściem jest użycie tylko reguł `exclude`, aby usunąć wszystkie
	pliki i katalogi, których nie chcesz przetwarzać. Na przykład:

		:# Wyklucza dowolny plik o nazwie `*.unrecoverable`
		:exclude *.unrecoverable
		:# Wyklucza katalog główny `/lost+found`
		:exclude /lost+found/
		:# Wyklucza dowolny podkatalog o nazwie `tmp`
		:exclude tmp/

	Odwrotnym podejściem jest zdefiniowanie tylko plików, które chcesz przetwarzać,
	używając tylko reguł `include`. Na przykład:

		:# Włącza tylko niektóre katalogi
		:include /movies/
		:include /musics/
		:include /pictures/

	Ostatecznym podejściem jest mieszanie reguł `exclude` i `include`. W tym przypadku
	kolejność reguł jest ważna. Wcześniejsze reguły mają
	pierwszeństwo przed późniejszymi.
	Aby uprościć, możesz najpierw wymienić wszystkie reguły `exclude`, a następnie
	wszystkie reguły `include`. Na przykład:

		:# Wyklucza dowolny plik o nazwie `*.unrecoverable`
		:exclude *.unrecoverable
		:# Wyklucza dowolny podkatalog o nazwie `tmp`
		:exclude tmp/
		:# Włącza tylko niektóre katalogi
		:include /movies/
		:include /musics/
		:include /pictures/

	W wierszu poleceń, używając opcji -f, możesz użyć tylko wzorców `include`.
	Na przykład:

		:# Sprawdza tylko pliki .mp3.
		:# W systemie Unix użyj cudzysłowów, aby uniknąć rozszerzenia globbing przez powłokę.
		:snapraid -f "*.mp3" check

	W systemie Unix, używając znaków globbing w wierszu poleceń, musisz
	je cytować, aby zapobiec ich rozszerzeniu przez powłokę.

Ignore File
	Oprócz reguł globalnych w pliku konfiguracyjnym, możesz umieścić pliki
	`.snapraidignore` w dowolnym katalogu w macierzy, aby zdefiniować
	zdecentralizowane reguły wykluczania.

	Reguły zdefiniowane w `.snapraidignore` są stosowane po regułach w pliku
	konfiguracyjnym. Oznacza to, że mają one wyższy priorytet i mogą być używane
	do wykluczania plików, które zostały wcześniej uwzględnione przez konfigurację
	globalną. Skutecznie, jeśli reguła lokalna pasuje, plik jest wykluczany bez
	względu na globalne ustawienia uwzględniania.

	Logika wzorców w `.snapraidignore` odzwierciedla konfigurację globalną, ale
	zakotwicza wzorce w katalogu, w którym znajduje się plik:

	=FILE
		Wybiera dowolny plik o nazwie FILE w tym katalogu lub poniżej.
		Podlega to tym samym zasadom dopasowywania (globbing) co wzorzec globalny.

	=DIR/
		Wybiera dowolny katalog o nazwie DIR i wszystko w jego wnętrzu,
		znajdujący się w tym katalogu lub poniżej.

	=/PATH/FILE
		Wybiera dokładnie określony plik względem lokalizacji
		pliku `.snapraidignore`.

	=/PATH/DIR/
		Wybiera dokładnie określony katalog i wszystko w jego wnętrzu,
		względem lokalizacji pliku `.snapraidignore`.

	W przeciwieństwie do konfiguracji globalnej, pliki `.snapraidignore` obsługują
	tylko reguły wykluczania; nie można używać wzorców `include` ani negacji (!).

	Na przykład, jeśli masz `.snapraidignore` w `/mnt/disk1/projects/`:

		:# Wyklucza TYLKO /mnt/disk1/projects/output.bin
		:/output.bin
		:# Wyklucza każdy katalog o nazwie `build` wewnątrz projects/
		:build/
		:# Wyklucza każdy plik .tmp wewnątrz projects/ lub jego podfolderach
		:*.tmp

Content
	SnapRAID przechowuje listę i sumy kontrolne Twoich plików w pliku zawartości.

	Jest to plik binarny, który zawiera listę wszystkich plików obecnych w Twojej macierzy dysków,
	wraz ze wszystkimi sumami kontrolnymi w celu weryfikacji ich integralności.

	Ten plik jest odczytywany i zapisywany przez polecenia `sync` i `scrub`
	oraz odczytywany przez polecenia `fix`, `check` i `status`.

Parity
	SnapRAID przechowuje informacje o parzystości Twojej macierzy w plikach
	parzystości.

	Są to pliki binarne zawierające obliczoną parzystość wszystkich
	bloków zdefiniowanych w pliku `content`.

	Pliki te są odczytywane i zapisywane przez polecenia `sync` i `fix`
	oraz tylko odczytywane przez polecenia `scrub` i `check`.

Encoding
	SnapRAID w systemie Unix ignoruje wszelkie kodowanie. Odczytuje i przechowuje
	nazwy plików z tym samym kodowaniem używanym przez system plików.

	W systemie Windows wszystkie nazwy odczytane z systemu plików są konwertowane i
	przetwarzane w formacie UTF-8.

	Aby nazwy plików były poprawnie drukowane, musisz ustawić konsolę Windows
	na tryb UTF-8 za pomocą polecenia `chcp 65001` i użyć
	czcionki TrueType, takiej jak `Lucida Console`, jako czcionki konsoli.
	Dotyczy to tylko drukowanych nazw plików; jeśli
	przekierujesz wyjście konsoli do pliku, wynikowy plik jest zawsze
	w formacie UTF-8.

Copyright
	Ten plik jest chroniony prawem autorskim (C) 2025 Andrea Mazzoleni

See Also
	snapraid_log(1), snapraidd(1)

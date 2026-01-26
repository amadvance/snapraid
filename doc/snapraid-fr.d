Name{number}
	snapraid - Sauvegarde SnapRAID pour baies de disques

Synopsis
	:snapraid [-c, --conf CONFIG]
	:	[-f, --filter MOTIF] [-d, --filter-disk NOM]
	:	[-m, --filter-missing] [-e, --filter-error]
	:	[-a, --audit-only] [-h, --pre-hash] [-i, --import DOSSIER]
	:	[-p, --plan PERC|mauvais|nouveau|complet]
	:	[-o, --older-than JOURS] [-l, --log FICHIER]
	:	[-s, --spin-down-on-error] [-w, --bw-limit DÉBIT]
	:	[-Z, --force-zero] [-E, --force-empty]
	:	[-U, --force-uuid] [-D, --force-device]
	:	[-N, --force-nocopy] [-F, --force-full]
	:	[-R, --force-realloc]
	:	[-S, --start BLKSTART] [-B, --count BLKCOUNT]
	:	[-L, --error-limit NOMBRE]
	:	[-A, --stats]
	:	[-v, --verbose] [-q, --quiet]
	:	status|smart|probe|up|down|diff|sync|scrub|fix|check
	:	|list|dup|pool|devices|touch|rehash

	:snapraid [-V, --version] [-H, --help] [-C, --gen-conf CONTENU]

Description
	SnapRAID est un programme de sauvegarde conçu pour les baies de disques,
	stockant les informations de parité pour la récupération des données en cas
	de défaillance de six disques maximum.

	Principalement destiné aux centres multimédia domestiques avec des fichiers
	volumineux et peu souvent modifiés, SnapRAID offre plusieurs fonctionnalités :

	* Vous pouvez utiliser des disques déjà remplis de fichiers sans avoir besoin
		de les reformater, en y accédant comme d'habitude.
	* Toutes vos données sont hachées pour garantir l'intégrité des données
		et prévenir la corruption silencieuse.
	* Lorsque le nombre de disques défaillants dépasse le nombre de parités,
		la perte de données est limitée aux disques affectés ; les données
		sur les autres disques restent accessibles.
	* Si vous supprimez accidentellement des fichiers sur un disque,
		la récupération est possible.
	* Les disques peuvent avoir des tailles différentes.
	* Vous pouvez ajouter des disques à tout moment.
	* SnapRAID ne verrouille pas vos données ; vous pouvez arrêter de l'utiliser
		à tout moment sans reformater ni déplacer de données.
	* Pour accéder à un fichier, un seul disque doit tourner, ce qui
		économise de l'énergie et réduit le bruit.

	Pour plus d'informations, veuillez visiter le site officiel de SnapRAID :

		:https://www.snapraid.it/

Limitations
	SnapRAID est un hybride entre un programme RAID et un programme de sauvegarde,
	visant à combiner les meilleurs avantages des deux. Cependant, il a quelques
	limitations que vous devriez considérer avant de l'utiliser.

	La principale limitation est que si un disque tombe en panne et que vous
	n'avez pas synchronisé récemment, vous pourriez ne pas être en mesure de
	récupérer complètement.
	Plus précisément, vous pourriez ne pas être en mesure de récupérer jusqu'à
	la taille des fichiers modifiés ou supprimés depuis la dernière opération
	de synchronisation.
	Cela se produit même si les fichiers modifiés ou supprimés ne se trouvent
	pas sur le disque défaillant. C'est pourquoi SnapRAID est mieux adapté
	aux données qui changent rarement.

	D'un autre côté, les fichiers nouvellement ajoutés n'empêchent pas
	la récupération des fichiers déjà existants. Vous ne perdrez que les
	fichiers récemment ajoutés s'ils se trouvent sur le disque défaillant.

	Les autres limitations de SnapRAID sont :

	* Avec SnapRAID, vous avez toujours des systèmes de fichiers séparés
		pour chaque disque. Avec le RAID, vous obtenez un seul grand
		système de fichiers.
	* SnapRAID ne fait pas de striping (entrelacement) des données.
		Avec le RAID, vous obtenez un gain de vitesse avec le striping.
	* SnapRAID ne prend pas en charge la récupération en temps réel.
		Avec le RAID, vous n'avez pas à arrêter de travailler lorsqu'un
		disque tombe en panne.
	* SnapRAID ne peut récupérer des données que pour un nombre limité
		de défaillances de disques. Avec une sauvegarde, vous pouvez
		récupérer d'une défaillance complète de toute la baie de disques.
	* Seuls les noms de fichiers, les horodatages, les liens symboliques
		et les liens matériels sont sauvegardés. Les permissions, la
		propriété et les attributs étendus ne sont pas sauvegardés.

Démarrer
	Pour utiliser SnapRAID, vous devez d'abord sélectionner un disque
	dans votre baie de disques pour le dédier aux informations de `parité`.
	Avec un disque pour la parité, vous pourrez récupérer d'une
	seule défaillance de disque, similaire au RAID5.

	Si vous souhaitez récupérer de plus de défaillances de disques,
	similaire au RAID6, vous devez réserver des disques supplémentaires
	pour la parité. Chaque disque de parité supplémentaire permet la
	récupération d'une défaillance de disque de plus.

	Comme disques de parité, vous devez choisir les plus grands disques
	de la baie, car les informations de parité peuvent atteindre la
	taille du plus grand disque de données de la baie.

	Ces disques seront dédiés au stockage des fichiers de `parité`.
	Vous ne devriez pas y stocker vos données.

	Ensuite, vous devez définir les disques de `données` que vous souhaitez
	protéger avec SnapRAID. La protection est plus efficace si ces disques
	contiennent des données qui changent rarement. Pour cette raison, il est
	préférable de NE PAS inclure le disque C:\ de Windows ou les répertoires
	/home, /var et /tmp d'Unix.

	La liste des fichiers est enregistrée dans les fichiers `content` (contenu),
	généralement stockés sur les disques de données, de parité ou de démarrage.
	Ce fichier contient les détails de votre sauvegarde, y compris toutes les
	sommes de contrôle pour vérifier son intégrité.
	Le fichier `content` est stocké en plusieurs copies, et chaque copie
	doit se trouver sur un disque différent pour garantir qu'au moins une
	copie soit disponible, même en cas de défaillances de disques multiples.

	Par exemple, supposons que vous ne soyez intéressé que par un seul
	niveau de protection de parité, et que vos disques se trouvent à :

		:/mnt/diskp <- disque sélectionné pour la parité
		:/mnt/disk1 <- premier disque à protéger
		:/mnt/disk2 <- deuxième disque à protéger
		:/mnt/disk3 <- troisième disque à protéger

	Vous devez créer le fichier de configuration /etc/snapraid.conf
	avec les options suivantes :

		:parity /mnt/diskp/snapraid.parity
		:content /var/snapraid/snapraid.content
		:content /mnt/disk1/snapraid.content
		:content /mnt/disk2/snapraid.content
		:data d1 /mnt/disk1/
		:data d2 /mnt/disk2/
		:data d3 /mnt/disk3/

	Si vous êtes sous Windows, vous devez utiliser le format de chemin
	Windows, avec des lettres de lecteur et des barres obliques inverses
	au lieu de barres obliques.

		:parity E:\snapraid.parity
		:content C:\snapraid\snapraid.content
		:content F:\array\snapraid.content
		:content G:\array\snapraid.content
		:data d1 F:\array\
		:data d2 G:\array\
		:data d3 H:\array\

	Si vous avez de nombreux disques et manquez de lettres de lecteur,
	vous pouvez monter les disques directement dans des sous-dossiers. Voir :

		:https://www.google.com/search?q=Windows+mount+point

	À ce stade, vous êtes prêt à exécuter la commande `sync` (synchroniser)
	pour construire les informations de parité.

		:snapraid sync

	Ce processus peut prendre plusieurs heures la première fois, en fonction
	de la taille des données déjà présentes sur les disques. Si les disques
	sont vides, le processus est immédiat.

	Vous pouvez l'arrêter à tout moment en appuyant sur Ctrl+C, et lors de
	la prochaine exécution, il reprendra là où il a été interrompu.

	Lorsque cette commande est terminée, vos données sont EN SÉCURITÉ.

	Vous pouvez maintenant commencer à utiliser votre baie comme vous le souhaitez
	et mettre à jour périodiquement les informations de parité en exécutant
	la commande `sync`.

  Nettoyage (Scrubbing)
	Pour vérifier périodiquement les données et la parité à la recherche
	d'erreurs, vous pouvez exécuter la commande `scrub`.

		:snapraid scrub

	Cette commande compare les données de votre baie avec le hachage calculé
	lors de la commande `sync` pour vérifier l'intégrité.

	Chaque exécution de la commande vérifie environ 8 % de la baie, en
	excluant les données déjà nettoyées au cours des 10 jours précédents.
	Vous pouvez utiliser l'option -p, --plan pour spécifier une quantité
	différente et l'option -o, --older-than pour spécifier un âge
	différent en jours. Par exemple, pour vérifier 5 % de la baie
	pour les blocs de plus de 20 jours, utilisez :

		:snapraid -p 5 -o 20 scrub

	Si des erreurs silencieuses ou d'entrée/sortie sont trouvées pendant
	le processus, les blocs correspondants sont marqués comme mauvais
	dans le fichier `content` et listés dans la commande `status`.

		:snapraid status

	Pour les corriger, vous pouvez utiliser la commande `fix`, en filtrant
	les mauvais blocs avec l'option -e, --filter-error :

		:snapraid -e fix

	Lors du prochain `scrub`, les erreurs disparaîtront du rapport
	`status` si elles sont réellement corrigées. Pour accélérer,
	vous pouvez utiliser -p mauvais pour nettoyer uniquement les blocs
	marqués comme mauvais.

		:snapraid -p bad scrub

	L'exécution de `scrub` sur une baie non synchronisée peut signaler
	des erreurs causées par des fichiers supprimés ou modifiés. Ces erreurs
	sont signalées dans la sortie de `scrub`, mais les blocs associés
	ne sont pas marqués comme mauvais.

  Mise en commun (Pooling)
	Note : La fonctionnalité de mise en commun décrite ci-dessous a été
	remplacée par l'outil mergefs, qui est désormais l'option recommandée
	pour les utilisateurs Linux de la communauté SnapRAID. Mergefs offre
	un moyen plus flexible et efficace de regrouper plusieurs disques
	dans un seul point de montage unifié, permettant un accès transparent
	aux fichiers sur toute votre baie sans dépendre de liens symboliques.
	Il s'intègre bien avec SnapRAID pour la protection par parité et est
	couramment utilisé dans des configurations comme OpenMediaVault (OMV)
	ou des configurations NAS personnalisées.

	Pour que tous les fichiers de votre baie apparaissent dans la même
	arborescence de répertoires, vous pouvez activer la fonctionnalité
	de `pooling`. Elle crée une vue virtuelle en lecture seule de tous
	les fichiers de votre baie à l'aide de liens symboliques.

	Vous pouvez configurer le répertoire de `pooling` dans le fichier
	de configuration avec :

		:pool /pool

	ou, si vous êtes sous Windows, avec :

		:pool C:\pool

	puis exécutez la commande `pool` pour créer ou mettre à jour
	la vue virtuelle.

		:snapraid pool

	Si vous utilisez une plateforme Unix et souhaitez partager ce
	répertoire sur le réseau avec des machines Windows ou Unix,
	vous devez ajouter les options suivantes à votre
	/etc/samba/smb.conf :

		:# Dans la section globale de smb.conf
		:unix extensions = no

		:# Dans la section de partage de smb.conf
		:[pool]
		:comment = Pool
		:path = /pool
		:read only = yes
		:guest ok = yes
		:wide links = yes
		:follow symlinks = yes

	Sous Windows, le partage de liens symboliques sur un réseau
	nécessite que les clients les résolvent à distance. Pour
	activer cela, en plus de partager le répertoire du pool,
	vous devez également partager tous les disques indépendamment,
	en utilisant les noms de disques définis dans le fichier de
	configuration comme points de partage. Vous devez également
	spécifier dans l'option `share` du fichier de configuration
	le chemin UNC Windows que les clients distants doivent
	utiliser pour accéder à ces disques partagés.

	Par exemple, en opérant à partir d'un serveur nommé `darkstar`,
	vous pouvez utiliser les options :

		:data d1 F:\array\
		:data d2 G:\array\
		:data d3 H:\array\
		:pool C:\pool
		:share \\darkstar

	et partager les répertoires suivants sur le réseau :

		:\\darkstar\pool -> C:\pool
		:\\darkstar\d1 -> F:\array
		:\\darkstar\d2 -> G:\array
		:\\darkstar\d3 -> H:\array

	pour permettre aux clients distants d'accéder à tous les fichiers
	à \\darkstar\pool.

	Vous pourriez également avoir besoin de configurer les clients
	distants pour activer l'accès aux liens symboliques distants avec
	la commande :

		:fsutil behavior set SymlinkEvaluation L2L:1 R2R:1 L2R:1 R2L:1

  Restauration (Undeleting)
	SnapRAID fonctionne davantage comme un programme de sauvegarde
	qu'un système RAID, et il peut être utilisé pour restaurer
	ou récupérer des fichiers dans leur état précédent à l'aide
	de l'option -f, --filter :

		:snapraid fix -f FICHIER

	ou pour un répertoire :

		:snapraid fix -f DOSSIER/

	Vous pouvez également l'utiliser pour récupérer uniquement les
	fichiers accidentellement supprimés à l'intérieur d'un
	répertoire en utilisant l'option -m, --filter-missing,
	qui ne restaure que les fichiers manquants, laissant tous
	les autres intacts.

		:snapraid fix -m -f DOSSIER/

	Ou pour récupérer tous les fichiers supprimés sur tous les
	lecteurs avec :

		:snapraid fix -m

  Récupération
	Le pire est arrivé, et vous avez perdu un ou plusieurs disques !

	NE PANIQUEZ PAS ! Vous pourrez les récupérer !

	La première chose à faire est d'éviter d'autres modifications
	sur votre baie de disques. Désactivez toute connexion à distance
	et tout processus planifié, y compris toute synchronisation ou
	nettoyage SnapRAID nocturne planifié.

	Procédez ensuite aux étapes suivantes.

    ÉTAPE 1 -> Reconfigurer
	Vous avez besoin d'espace pour récupérer, idéalement sur des disques
	de rechange supplémentaires, mais un disque USB externe ou un disque
	distant suffira.

	Modifiez le fichier de configuration SnapRAID pour que l'option
	`data` ou `parity` du disque défaillant pointe vers un emplacement
	avec suffisamment d'espace vide pour récupérer les fichiers.

	Par exemple, si le disque `d1` a échoué, passez de :

		:data d1 /mnt/disk1/

	à :

		:data d1 /mnt/new_spare_disk/

	Si le disque à récupérer est un disque de parité, mettez à jour
	l'option `parity` appropriée.
	Si vous avez plusieurs disques défaillants, mettez à jour toutes
	leurs options de configuration.

    ÉTAPE 2 -> Corriger (Fix)
	Exécutez la commande fix, en stockant le journal dans un fichier
	externe avec :

		:snapraid -d NOM -l fix.log fix

	Où NOM est le nom du disque, tel que `d1` dans notre exemple précédent.
	Si le disque à récupérer est un disque de parité, utilisez les noms
	`parity`, `2-parity`, etc.
	Si vous avez plusieurs disques défaillants, utilisez plusieurs
	options -d pour les spécifier tous.

	Cette commande prendra beaucoup de temps.

	Assurez-vous d'avoir quelques gigaoctets libres pour stocker le fichier fix.log.
	Exécutez-la à partir d'un disque avec suffisamment d'espace libre.

	Vous avez maintenant récupéré tout ce qui est récupérable. Si certains
	fichiers sont partiellement ou totalement irrécupérables, ils seront
	renommés en ajoutant l'extension `.unrecoverable`.

	Vous pouvez trouver une liste détaillée de tous les blocs irrécupérables
	dans le fichier fix.log en vérifiant toutes les lignes commençant par
	`unrecoverable:`.

	Si la récupération ne vous satisfait pas, vous pouvez réessayer autant
	de fois que vous le souhaitez.

	Par exemple, si vous avez supprimé des fichiers de la baie après le dernier
	`sync`, cela peut entraîner la non-récupération de certains fichiers.
	Dans ce cas, vous pouvez réessayer le `fix` en utilisant l'option
	-i, --import, en spécifiant où se trouvent maintenant ces fichiers
	pour les inclure à nouveau dans le processus de récupération.

	Si vous êtes satisfait de la récupération, vous pouvez continuer,
	mais notez qu'après la synchronisation, vous ne pouvez plus réessayer
	la commande `fix` !

    ÉTAPE 3 -> Vérifier (Check)
	Par précaution, vous pouvez maintenant exécuter une commande `check`
	pour vous assurer que tout est correct sur le disque récupéré.

		:snapraid -d NOM -a check

	Où NOM est le nom du disque, tel que `d1` dans notre exemple précédent.

	Les options -d et -a indiquent à SnapRAID de vérifier uniquement le
	disque spécifié et d'ignorer toutes les données de parité.

	Cette commande prendra beaucoup de temps, mais si vous n'êtes pas
	trop prudent, vous pouvez la sauter.

    ÉTAPE 4 -> Synchroniser (Sync)
	Exécutez la commande `sync` pour resynchroniser la baie avec le nouveau
	disque.

		:snapraid sync

	Si tout est récupéré, cette commande est immédiate.

Commandes
	SnapRAID fournit quelques commandes simples qui vous permettent de :

	* Afficher l'état de la baie de disques -> `status`
	* Contrôler les disques -> `smart`, `probe`, `up`, `down`
	* Faire une sauvegarde/instantané -> `sync`
	* Vérifier périodiquement les données -> `scrub`
	* Restaurer la dernière sauvegarde/instantané -> `fix`.

	Les commandes doivent être écrites en minuscules.

  status
	Affiche un résumé de l'état de la baie de disques.

	Il comprend des informations sur la fragmentation de la parité, l'âge
	des blocs sans vérification, et toutes les erreurs silencieuses
	enregistrées rencontrées lors du nettoyage.

	Les informations présentées se réfèrent au dernier moment où vous avez
	exécuté `sync`. Les modifications ultérieures ne sont pas prises en compte.

	Si des mauvais blocs ont été détectés, leurs numéros de blocs sont listés.
	Pour les corriger, vous pouvez utiliser la commande `fix -e`.

	Il montre également un graphique représentant la dernière fois que chaque
	bloc a été nettoyé ou synchronisé. Les blocs nettoyés sont affichés
	avec '*', les blocs synchronisés mais pas encore nettoyés avec 'o'.

	Rien n'est modifié.

  smart
	Affiche un rapport SMART de tous les disques du système.

	Il comprend une estimation de la probabilité de défaillance au cours
	de la prochaine année, vous permettant de planifier les remplacements
	de maintenance des disques qui présentent des attributs suspects.

	Cette estimation de probabilité est obtenue en corrélant les attributs
	SMART des disques avec les données Backblaze disponibles à :

		:https://www.backblaze.com/hard-drive-test-data.html

	Si SMART signale qu'un disque est en panne, `FAIL` ou `PREFAIL` est
	affiché pour ce disque, et SnapRAID retourne une erreur.
	Dans ce cas, le remplacement immédiat du disque est fortement
	recommandé.

	Les autres chaînes d'état possibles sont :
		logfail - Dans le passé, certains attributs étaient inférieurs
			au seuil.
		logerr - Le journal d'erreurs du périphérique contient des erreurs.
		selferr - Le journal d'auto-test du périphérique contient des erreurs.

	Si l'option -v, --verbose est spécifiée, une analyse statistique
	plus approfondie est fournie. Cette analyse peut vous aider à
	décider si vous avez besoin de plus ou moins de parité.

	Cette commande utilise l'outil `smartctl` et équivaut à exécuter
	`smartctl -a` sur tous les périphériques.

	Si vos périphériques ne sont pas détectés automatiquement correctement,
	vous pouvez spécifier une commande personnalisée en utilisant l'option
	`smartctl` dans le fichier de configuration.

	Rien n'est modifié.

  probe
	Affiche l'état d'ALIMENTATION de tous les disques du système.

	`Standby` signifie que le disque ne tourne pas. `Active` signifie
	que le disque tourne.

	Cette commande utilise l'outil `smartctl` et équivaut à exécuter
	`smartctl -n standby -i` sur tous les périphériques.

	Si vos périphériques ne sont pas détectés automatiquement correctement,
	vous pouvez spécifier une commande personnalisée en utilisant l'option
	`smartctl` dans le fichier de configuration.

	Rien n'est modifié.

  up
	Fait tourner (spin up) tous les disques de la baie.

	Vous pouvez faire tourner uniquement des disques spécifiques
	en utilisant l'option -d, --filter-disk.

	Faire tourner tous les disques en même temps nécessite beaucoup
	d'énergie. Assurez-vous que votre alimentation peut le supporter.

	Rien n'est modifié.

  down
	Arrête (spin down) tous les disques de la baie.

	Cette commande utilise l'outil `smartctl` et équivaut à exécuter
	`smartctl -s standby,now` sur tous les périphériques.

	Vous pouvez arrêter uniquement des disques spécifiques
	en utilisant l'option -d, --filter-disk.

	Pour arrêter automatiquement en cas d'erreur, vous pouvez utiliser
	l'option -s, --spin-down-on-error avec toute autre commande,
	ce qui équivaut à exécuter `down` manuellement lorsqu'une
	erreur se produit.

	Rien n'est modifié.

  diff
	Liste tous les fichiers modifiés depuis le dernier `sync` qui
	nécessitent la recomputation de leurs données de parité.

	Cette commande ne vérifie pas les données des fichiers,
	mais uniquement l'horodatage, la taille et l'inode du fichier.

	Après avoir listé tous les fichiers modifiés, un résumé des
	modifications est présenté, regroupé par :
		equal - Fichiers inchangés par rapport à avant.
		added - Fichiers ajoutés qui n'étaient pas présents auparavant.
		removed - Fichiers supprimés.
		updated - Fichiers avec une taille ou un horodatage différent,
			signifiant qu'ils ont été modifiés.
		moved - Fichiers déplacés vers un répertoire différent sur le
			même disque. Ils sont identifiés par le même nom, la
			même taille, le même horodatage et le même inode, mais
			un répertoire différent.
		copied - Fichiers copiés sur le même disque ou un disque différent.
			Notez que s'ils sont vraiment déplacés vers un autre
			disque, ils seront également comptés dans `removed`.
			Ils sont identifiés par le même nom, la même taille
			et le même horodatage. Si l'horodatage en sous-seconde
			est zéro, le chemin complet doit correspondre, pas seulement
			le nom.
		restored - Fichiers avec un inode différent mais correspondant
			au nom, à la taille et à l'horodatage. Il s'agit
			généralement de fichiers restaurés après avoir été supprimés.

	Si un `sync` est requis, le code de retour du processus est 2,
	au lieu du 0 par défaut. Le code de retour 1 est utilisé pour
	une condition d'erreur générique.

	Rien n'est modifié.

  sync
	Met à jour les informations de parité. Tous les fichiers modifiés
	dans la baie de disques sont lus, et les données de parité
	correspondantes sont mises à jour.

	Vous pouvez arrêter ce processus à tout moment en appuyant sur Ctrl+C,
	sans perdre le travail déjà effectué.
	Lors de la prochaine exécution, le processus `sync` reprendra
	là où il a été interrompu.

	Si des erreurs silencieuses ou d'entrée/sortie sont trouvées
	pendant le processus, les blocs correspondants sont marqués comme mauvais.

	Les fichiers sont identifiés par chemin et/ou inode et vérifiés
	par taille et horodatage.
	Si la taille ou l'horodatage du fichier diffère, les données de
	parité sont recomptées pour l'ensemble du fichier.
	Si le fichier est déplacé ou renommé sur le même disque, en gardant
	le même inode, la parité n'est pas recomptée.
	Si le fichier est déplacé vers un autre disque, la parité est
	recomptée, mais les informations de hachage précédemment calculées
	sont conservées.

	Les fichiers `content` et `parity` sont modifiés si nécessaire.
	Les fichiers dans la baie NE SONT PAS modifiés.

  scrub
	Nettoie la baie, vérifiant les erreurs silencieuses ou d'entrée/sortie
	dans les disques de données et de parité.

	Chaque invocation vérifie environ 8 % de la baie, en excluant
	les données déjà nettoyées au cours des 10 derniers jours.
	Cela signifie qu'un nettoyage une fois par semaine garantit que
	chaque bit de données est vérifié au moins une fois tous les trois mois.

	Vous pouvez définir un plan de nettoyage ou une quantité différente
	en utilisant l'option -p, --plan, qui accepte :
	bad - Nettoyer les blocs marqués mauvais.
	new - Nettoyer les blocs juste synchronisés qui n'ont pas encore été nettoyés.
	full - Nettoyer tout.
	0-100 - Nettoyer le pourcentage spécifié de blocs.

	Si vous spécifiez un pourcentage, vous pouvez également utiliser
	l'option -o, --older-than pour définir l'âge que le bloc
	doit avoir.
	Les blocs les plus anciens sont nettoyés en premier, assurant
	une vérification optimale.
	Si vous souhaitez nettoyer uniquement les blocs juste synchronisés
	qui n'ont pas encore été nettoyés, utilisez l'option `-p new`.

	Pour obtenir des détails sur l'état du nettoyage, utilisez la commande
	`status`.

	Pour toute erreur silencieuse ou d'entrée/sortie trouvée, les blocs
	correspondants sont marqués comme mauvais dans le fichier `content`.
	Ces mauvais blocs sont listés dans `status` et peuvent être corrigés
	avec `fix -e`.
	Après la correction, lors du prochain nettoyage, ils seront revérifiés,
	et s'ils sont jugés corrigés, la marque "mauvais" sera supprimée.
	Pour nettoyer uniquement les mauvais blocs, vous pouvez utiliser la
	commande `scrub -p bad`.

	Il est recommandé d'exécuter `scrub` uniquement sur une baie
	synchronisée pour éviter les erreurs signalées causées par des données
	non synchronisées. Ces erreurs sont reconnues comme n'étant pas
	des erreurs silencieuses, et les blocs ne sont pas marqués comme mauvais,
	mais de telles erreurs sont signalées dans la sortie de la commande.

	Le fichier `content` est modifié pour mettre à jour l'heure de la dernière
	vérification de chaque bloc et pour marquer les mauvais blocs.
	Les fichiers `parity` NE SONT PAS modifiés.
	Les fichiers dans la baie NE SONT PAS modifiés.

  fix
	Corrige tous les fichiers et les données de parité.

	Tous les fichiers et les données de parité sont comparés à l'état
	de l'instantané enregistré lors du dernier `sync`.
	Si une différence est trouvée, elle est rétablie à l'instantané stocké.

	AVERTISSEMENT ! La commande `fix` ne fait pas de distinction entre
	les erreurs et les modifications intentionnelles. Elle rétablit
	inconditionnellement l'état du fichier au dernier `sync`.

	Si aucune autre option n'est spécifiée, l'intégralité de la baie est traitée.
	Utilisez les options de filtre pour sélectionner un sous-ensemble de
	fichiers ou de disques sur lesquels opérer.

	Pour corriger uniquement les blocs marqués mauvais pendant `sync` et `scrub`,
	utilisez l'option -e, --filter-error.
	Contrairement aux autres options de filtre, celle-ci applique des corrections
	uniquement aux fichiers qui sont inchangés depuis le dernier `sync`.

	SnapRAID renomme tous les fichiers qui ne peuvent pas être corrigés
	en ajoutant l'extension `.unrecoverable`.

	Avant de corriger, l'intégralité de la baie est scannée pour trouver
	tout fichier déplacé depuis la dernière opération `sync`.
	Ces fichiers sont identifiés par leur horodatage, ignorant leur nom
	et leur répertoire, et sont utilisés dans le processus de récupération
	si nécessaire.
	Si vous en avez déplacé certains en dehors de la baie, vous pouvez
	utiliser l'option -i, --import pour spécifier des répertoires
	supplémentaires à scanner.

	Les fichiers sont identifiés uniquement par chemin, pas par inode.

	Le fichier `content` N'EST PAS modifié.
	Les fichiers `parity` sont modifiés si nécessaire.
	Les fichiers dans la baie sont modifiés si nécessaire.

  check
	Vérifie tous les fichiers et les données de parité.

	Il fonctionne comme `fix`, mais il simule seulement une récupération
	et aucune modification n'est écrite sur la baie.

	Cette commande est principalement destinée à la vérification manuelle,
	comme après un processus de récupération ou dans d'autres conditions
	spéciales. Pour les vérifications périodiques et planifiées, utilisez
	`scrub`.

	Si vous utilisez l'option -a, --audit-only, seules les données
	des fichiers sont vérifiées, et les données de parité sont ignorées
	pour une exécution plus rapide.

	Les fichiers sont identifiés uniquement par chemin, pas par inode.

	Rien n'est modifié.

  list
	Liste tous les fichiers contenus dans la baie au moment du dernier
	`sync`.

	Avec -v ou --verbose, l'heure en sous-seconde est également affichée.

	Rien n'est modifié.

  dup
	Liste tous les fichiers en double. Deux fichiers sont considérés
	comme égaux si leurs hachages correspondent. Les données des fichiers
	ne sont pas lues ; seuls les hachages précalculés sont utilisés.

	Rien n'est modifié.

  pool
	Crée ou met à jour une vue virtuelle de tous
	les fichiers de votre baie de disques dans le répertoire `pooling`.

	Les fichiers ne sont pas copiés mais liés à l'aide de
	liens symboliques.

	Lors de la mise à jour, tous les liens symboliques et les
	sous-répertoires vides existants sont supprimés et remplacés par
	la nouvelle vue de la baie. Tout autre fichier ordinaire est laissé
	en place.

	Rien n'est modifié en dehors du répertoire du pool.

  devices
	Affiche les périphériques de bas niveau utilisés par la baie.

	Cette commande affiche les associations de périphériques dans la baie
	et est principalement destinée à être une interface de script.

	Les deux premières colonnes sont l'ID et le chemin du périphérique
	de bas niveau. Les deux colonnes suivantes sont l'ID et le chemin
	du périphérique de haut niveau. La dernière colonne est le nom du
	disque dans la baie.

	Dans la plupart des cas, vous avez un périphérique de bas niveau
	pour chaque disque de la baie, mais dans certaines configurations
	plus complexes, vous pouvez avoir plusieurs périphériques de bas
	niveau utilisés par un seul disque de la baie.

	Rien n'est modifié.

  touch
	Définit un horodatage arbitraire en sous-seconde pour tous les fichiers
	qui l'ont défini à zéro.

	Cela améliore la capacité de SnapRAID à reconnaître les fichiers
	déplacés et copiés, car cela rend l'horodatage presque unique,
	réduisant les doublons possibles.

	Plus précisément, si l'horodatage en sous-seconde n'est pas zéro,
	un fichier déplacé ou copié est identifié comme tel s'il correspond
	au nom, à la taille et à l'horodatage. Si l'horodatage en sous-seconde
	est zéro, il est considéré comme une copie uniquement si le chemin
	complet, la taille et l'horodatage correspondent tous.

	L'horodatage à la seconde près n'est pas modifié,
	de sorte que toutes les dates et heures de vos fichiers seront
	préservées.

  rehash
	Planifie un nouveau hachage de toute la baie.

	Cette commande change le type de hachage utilisé, généralement lors
	de la mise à niveau d'un système 32 bits vers un système 64 bits,
	pour passer de MurmurHash3 au plus rapide SpookyHash.

	Si vous utilisez déjà le hachage optimal, cette commande
	ne fait rien et vous informe qu'aucune action n'est nécessaire.

	Le nouveau hachage n'est pas effectué immédiatement, mais a lieu
	progressivement pendant `sync` et `scrub`.

	Vous pouvez vérifier l'état du nouveau hachage en utilisant `status`.

	Pendant le nouveau hachage, SnapRAID conserve toutes ses
	fonctionnalités, à la seule exception que `dup` ne peut pas
	détecter les fichiers en double utilisant un hachage différent.

Options
	SnapRAID fournit les options suivantes :

	-c, --conf CONFIG
		Sélectionne le fichier de configuration à utiliser. S'il n'est pas
		spécifié, sous Unix, il utilise le fichier `/usr/local/etc/snapraid.conf`
		s'il existe, sinon `/etc/snapraid.conf`.
		Sous Windows, il utilise le fichier `snapraid.conf` dans le même
		répertoire que `snapraid.exe`.

	-f, --filter MOTIF
		Filtre les fichiers à traiter dans `check` et `fix`.
		Seuls les fichiers correspondant au motif spécifié sont traités.
		Cette option peut être utilisée plusieurs fois.
		Voir la section MOTIF pour plus de détails sur les
		spécifications de motifs.
		Sous Unix, assurez-vous que les caractères de globbing sont
		entre guillemets s'ils sont utilisés.
		Cette option ne peut être utilisée qu'avec `check` et `fix`.
		Elle ne peut pas être utilisée avec `sync` et `scrub`, car ils
		traitent toujours l'intégralité de la baie.

	-d, --filter-disk NOM
		Filtre les disques à traiter dans `check`, `fix`, `up` et `down`.
		Vous devez spécifier un nom de disque tel que défini dans le fichier
		de configuration.
		Vous pouvez également spécifier des disques de parité avec les noms :
		`parity`, `2-parity`, `3-parity`, etc., pour limiter les opérations
		à un disque de parité spécifique.
		Si vous combinez plusieurs options --filter, --filter-disk et
		--filter-missing, seuls les fichiers correspondant à tous les filtres
		sont sélectionnés.
		Cette option peut être utilisée plusieurs fois.
		Cette option ne peut être utilisée qu'avec `check`, `fix`, `up`
		et `down`.
		Elle ne peut pas être utilisée avec `sync` et `scrub`, car ils
		traitent toujours l'intégralité de la baie.

	-m, --filter-missing
		Filtre les fichiers à traiter dans `check` et `fix`.
		Seuls les fichiers manquants ou supprimés de la baie sont traités.
		Lorsqu'elle est utilisée avec `fix`, cela agit comme une commande
		`undelete` (restaurer).
		Si vous combinez plusieurs options --filter, --filter-disk et
		--filter-missing, seuls les fichiers correspondant à tous les filtres
		sont sélectionnés.
		Cette option ne peut être utilisée qu'avec `check` et `fix`.
		Elle ne peut pas être utilisée avec `sync` et `scrub`, car ils
		traitent toujours l'intégralité de la baie.

	-e, --filter-error
		Traite les fichiers avec des erreurs dans `check` et `fix`.
		Il ne traite que les fichiers qui ont des blocs marqués avec des erreurs
		silencieuses ou d'entrée/sortie pendant `sync` et `scrub`, tels
		que listés dans `status`.
		Cette option ne peut être utilisée qu'avec `check` et `fix`.

	-p, --plan PERC|bad|new|full
		Sélectionne le plan de nettoyage. Si PERC est une valeur numérique
		de 0 à 100, elle est interprétée comme le pourcentage de blocs à
		nettoyer.
		Au lieu d'un pourcentage, vous pouvez spécifier un plan :
		`bad` nettoie les mauvais blocs, `new` nettoie les blocs
		qui n'ont pas encore été nettoyés, et `full` nettoie tout.
		Cette option ne peut être utilisée qu'avec `scrub`.

	-o, --older-than JOURS
		Sélectionne la partie la plus ancienne de la baie à traiter dans `scrub`.
		JOURS est l'âge minimum en jours pour qu'un bloc soit nettoyé ;
		la valeur par défaut est 10.
		Les blocs marqués comme mauvais sont toujours nettoyés,
		quelle que soit cette option.
		Cette option ne peut être utilisée qu'avec `scrub`.

	-a, --audit-only
		Dans `check`, vérifie le hachage des fichiers sans
		vérifier les données de parité.
		Si vous êtes intéressé uniquement par la vérification des données
		des fichiers, cette option peut accélérer considérablement le
		processus de vérification.
		Cette option ne peut être utilisée qu'avec `check`.

	-h, --pre-hash
		Dans `sync`, exécute une phase de hachage préliminaire de toutes
		les nouvelles données pour une vérification supplémentaire avant
		le calcul de la parité.
		Habituellement, dans `sync`, aucun hachage préliminaire n'est
		effectué, et les nouvelles données sont hachées juste avant
		le calcul de la parité lorsqu'elles sont lues pour la première fois.
		Ce processus se produit lorsque le système est soumis à
		une lourde charge, avec tous les disques en rotation et un
		processeur occupé.
		Il s'agit d'une condition extrême pour la machine, et si elle
		présente un problème matériel latent, des erreurs silencieuses
		peuvent passer inaperçues car les données ne sont pas encore
		hachées.
		Pour éviter ce risque, vous pouvez activer le mode `pre-hash`
		pour que toutes les données soient lues deux fois afin de
		garantir leur intégrité.
		Cette option vérifie également les fichiers déplacés au sein
		de la baie pour s'assurer que l'opération de déplacement a
		réussi et, si nécessaire, vous permet d'exécuter une
		opération fix avant de continuer.
		Cette option ne peut être utilisée qu'avec `sync`.

	-i, --import DOSSIER
		Importe à partir du répertoire spécifié tous les fichiers supprimés
		de la baie après le dernier `sync`.
		Si vous avez toujours de tels fichiers, ils peuvent être utilisés
		par `check` et `fix` pour améliorer le processus de récupération.
		Les fichiers sont lus, y compris dans les sous-répertoires, et sont
		identifiés quel que soit leur nom.
		Cette option ne peut être utilisée qu'avec `check` et `fix`.

	-s, --spin-down-on-error
		En cas d'erreur, arrête (spin down) tous les disques gérés avant
		de quitter avec un code d'état non nul. Cela empêche les
		lecteurs de rester actifs et en rotation après une opération
		interrompue, aidant à éviter une accumulation inutile de chaleur
		et une consommation d'énergie. Utilisez cette option pour vous
		assurer que les disques sont arrêtés en toute sécurité
		même lorsqu'une commande échoue.

	-w, --bw-limit DÉBIT
		Applique une limite de bande passante globale pour tous les disques.
		Le DÉBIT est le nombre d'octets par seconde. Vous pouvez spécifier
		un multiplicateur tel que K, M ou G (par exemple, --bw-limit 1G).

	-A, --stats
		Active une vue d'état étendue qui affiche des informations
		supplémentaires.
		L'écran affiche deux graphiques :
		Le premier graphique montre le nombre de bandes mises en mémoire
		tampon pour chaque disque, ainsi que le chemin d'accès au fichier
		en cours d'accès sur ce disque. Généralement, le disque le plus
		lent n'aura pas de tampon disponible, ce qui détermine la
		bande passante maximale réalisable.
		Le deuxième graphique montre le pourcentage de temps passé en attente
		au cours des 100 dernières secondes. Le disque le plus lent est
		censé causer la majeure partie du temps d'attente, tandis que les
		autres disques devraient avoir peu ou pas de temps d'attente car
		ils peuvent utiliser leurs bandes mises en mémoire tampon.
		Ce graphique montre également le temps passé en attente pour les
		calculs de hachage et les calculs RAID.
		Tous les calculs s'exécutent en parallèle avec les opérations de disque.
		Par conséquent, tant qu'il y a un temps d'attente mesurable pour
		au moins un disque, cela indique que le processeur est suffisamment
		rapide pour suivre la charge de travail.

	-Z, --force-zero
		Force l'opération non sécurisée de synchronisation d'un fichier
		de taille zéro qui était auparavant non nul.
		Si SnapRAID détecte une telle condition, il arrête de
		procéder à moins que vous ne spécifiiez cette option.
		Cela vous permet de détecter facilement quand, après un
		plantage du système, certains fichiers accédés ont été tronqués.
		C'est une condition possible sous Linux avec les systèmes
		de fichiers ext3/ext4.
		Cette option ne peut être utilisée qu'avec `sync`.

	-E, --force-empty
		Force l'opération non sécurisée de synchronisation d'un disque
		avec tous les fichiers originaux manquants.
		Si SnapRAID détecte que tous les fichiers initialement présents
		sur le disque sont manquants ou réécrits, il arrête de
		procéder à moins que vous ne spécifiiez cette option.
		Cela vous permet de détecter facilement lorsqu'un système de
		fichiers de données n'est pas monté.
		Cette option ne peut être utilisée qu'avec `sync`.

	-U, --force-uuid
		Force l'opération non sécurisée de synchronisation, vérification
		et correction avec des disques qui ont changé leur UUID.
		Si SnapRAID détecte que certains disques ont changé d'UUID,
		il arrête de procéder à moins que vous ne spécifiiez cette option.
		Cela vous permet de détecter lorsque vos disques sont montés
		aux mauvais points de montage.
		Il est cependant autorisé d'avoir un seul changement d'UUID
		avec une seule parité, et plus avec une parité multiple,
		car c'est le cas normal lors du remplacement des disques après
		une récupération.
		Cette option ne peut être utilisée qu'avec `sync`, `check` ou
		`fix`.

	-D, --force-device
		Force l'opération non sécurisée de correction avec des disques
		inaccessibles ou avec des disques sur le même périphérique physique.
		Par exemple, si vous avez perdu deux disques de données et que
		vous avez un disque de rechange pour ne récupérer que le premier,
		vous pouvez ignorer le deuxième disque inaccessible.
		Ou, si vous souhaitez récupérer un disque dans l'espace libre
		restant sur un disque déjà utilisé, partageant le même
		périphérique physique.
		Cette option ne peut être utilisée qu'avec `fix`.

	-N, --force-nocopy
		Dans `sync`, `check` et `fix`, désactive l'heuristique de détection
		de copie.
		Sans cette option, SnapRAID suppose que les fichiers ayant les mêmes
		attributs, tels que le nom, la taille et l'horodatage, sont des copies
		avec les mêmes données.
		Cela permet d'identifier les fichiers copiés ou déplacés d'un disque
		à un autre et réutilise les informations de hachage déjà calculées
		pour détecter les erreurs silencieuses ou pour récupérer les fichiers
		manquants.
		Dans de rares cas, ce comportement peut entraîner des faux positifs
		ou un processus lent en raison de nombreuses vérifications de hachage,
		et cette option vous permet de résoudre de tels problèmes.
		Cette option ne peut être utilisée qu'avec `sync`, `check` et `fix`.

	-F, --force-full
		Dans `sync`, force un recomptage complet de la parité.
		Cette option peut être utilisée lorsque vous ajoutez un nouveau
		niveau de parité ou si vous êtes revenu à un ancien fichier
		de contenu utilisant des données de parité plus récentes.
		Au lieu de recréer la parité à partir de zéro, cela vous permet
		de réutiliser les hachages présents dans le fichier de contenu
		pour valider les données et maintenir la protection des données
		pendant le processus `sync` en utilisant les données de parité
		existantes.
		Cette option ne peut être utilisée qu'avec `sync`.

	-R, --force-realloc
		Dans `sync`, force une réallocation complète des fichiers et
		une reconstruction de la parité.
		Cette option peut être utilisée pour réallouer complètement
		tous les fichiers, en supprimant la fragmentation, tout en
		réutilisant les hachages présents dans le fichier de contenu
		pour valider les données.
		Cette option ne peut être utilisée qu'avec `sync`.
		AVERTISSEMENT ! Cette option est réservée aux experts, et il est
		fortement recommandé de ne pas l'utiliser.
		Vous N'AVEZ AUCUNE protection des données pendant l'opération `sync`.

	-l, --log FICHIER
		Écrit un journal détaillé dans le fichier spécifié.
		Si cette option n'est pas spécifiée, les erreurs inattendues
		sont imprimées à l'écran, ce qui peut entraîner une sortie
		excessive en cas de nombreuses erreurs. Lorsque -l, --log est
		spécifié, seules les erreurs fatales qui entraînent l'arrêt
		de SnapRAID sont imprimées à l'écran.
		Si le chemin commence par '>>', le fichier est ouvert
		en mode ajout. Les occurrences de '%D' et '%T' dans le nom sont
		remplacées par la date et l'heure au format YYYYMMDD et
		HHMMSS. Dans les fichiers batch Windows, vous devez doubler
		le caractère '%', par exemple, result-%%D.log. Pour utiliser
		'>>', vous devez encadrer le nom de guillemets, par exemple,
		`">>result.log"`.
		Pour sortir le journal sur la sortie standard ou l'erreur
		standard, vous pouvez utiliser `">&1"` et `">&2"`,
		respectivement.
		Consultez le fichier ou la page de manuel snapraid_log.txt
		pour les descriptions des balises de journal.

	-L, --error-limit NOMBRE
		Définit une nouvelle limite d'erreurs avant d'arrêter l'exécution.
		Par défaut, SnapRAID s'arrête s'il rencontre plus de 100
		erreurs d'entrée/sortie, indiquant qu'un disque est
		probablement en panne.
		Cette option affecte `sync` et `scrub`, qui sont autorisés
		à continuer après le premier ensemble d'erreurs de disque
		pour essayer de terminer leurs opérations.
		Cependant, `check` et `fix` s'arrêtent toujours à la première erreur.

	-S, --start BLKSTART
		Commence le traitement à partir du numéro de bloc spécifié.
		Cela peut être utile pour réessayer de vérifier ou de corriger
		des blocs spécifiques en cas de disque endommagé.
		Cette option est principalement destinée à la récupération manuelle avancée.

	-B, --count BLKCOUNT
		Ne traite que le nombre de blocs spécifié.
		Cette option est principalement destinée à la récupération manuelle avancée.

	-C, --gen-conf CONTENU
		Génère un fichier de configuration factice à partir d'un
		fichier de contenu existant.
		Le fichier de configuration est écrit sur la sortie standard
		et n'écrase pas un fichier existant.
		Ce fichier de configuration contient également les informations
		nécessaires pour reconstruire les points de montage du disque au
		cas où vous perdriez l'intégralité du système.

	-v, --verbose
		Affiche plus d'informations à l'écran.
		S'il est spécifié une fois, il affiche les fichiers exclus
		et des statistiques supplémentaires.
		Cette option n'a aucun effet sur les fichiers journaux.

	-q, --quiet
		Affiche moins d'informations à l'écran.
		S'il est spécifié une fois, il supprime la barre de progression ;
		deux fois, les opérations en cours ; trois fois, les messages
		d'information ; quatre fois, les messages d'état.
		Les erreurs fatales sont toujours imprimées à l'écran.
		Cette option n'a aucun effet sur les fichiers journaux.

	-H, --help
		Affiche un écran d'aide court.

	-V, --version
		Affiche la version du programme.

Configuration
	SnapRAID nécessite un fichier de configuration pour savoir où se trouve
	votre baie de disques et où stocker les informations de parité.

	Sous Unix, il utilise le fichier `/usr/local/etc/snapraid.conf` s'il existe,
	sinon `/etc/snapraid.conf`.
	Sous Windows, il utilise le fichier `snapraid.conf` dans le même
	répertoire que `snapraid.exe`.

	Il doit contenir les options suivantes (sensibles à la casse) :

  parity FICHIER [,FICHIER] ...
	Définit les fichiers à utiliser pour stocker les informations de parité.
	La parité permet une protection contre une seule défaillance
	de disque, similaire au RAID5.

	Vous pouvez spécifier plusieurs fichiers, qui doivent se trouver
	sur des disques différents.
	Lorsqu'un fichier ne peut plus grandir, le suivant est utilisé.
	L'espace total disponible doit être au moins aussi grand que le
	plus grand disque de données de la baie.

	Vous pouvez ajouter des fichiers de parité supplémentaires plus tard,
	mais vous ne pouvez pas les réorganiser ni les supprimer.

	Garder les disques de parité réservés à la parité garantit qu'ils
	ne deviennent pas fragmentés, améliorant les performances.

	Sous Windows, 256 Mo sont laissés inutilisés sur chaque disque pour
	éviter l'avertissement concernant les disques pleins.

	Cette option est obligatoire et ne peut être utilisée qu'une seule fois.

  (2,3,4,5,6)-parity FICHIER [,FICHIER] ...
	Définit les fichiers à utiliser pour stocker les informations de parité
	supplémentaires.

	Pour chaque niveau de parité spécifié, un niveau de protection
	supplémentaire est activé :

	* 2-parity active la double parité RAID6.
	* 3-parity active la triple parité.
	* 4-parity active la quadruple (quatre) parité.
	* 5-parity active la penta (cinq) parité.
	* 6-parity active l'hexa (six) parité.

	Chaque niveau de parité nécessite la présence de tous les niveaux
	de parité précédents.

	Les mêmes considérations que pour l'option 'parity' s'appliquent.

	Ces options sont facultatives et ne peuvent être utilisées qu'une seule fois.

  z-parity FICHIER [,FICHIER] ...
	Définit un fichier et un format alternatifs pour stocker la triple parité.

	Cette option est une alternative à '3-parity', principalement destinée
	aux processeurs bas de gamme comme ARM ou AMD Phenom, Athlon et Opteron
	qui ne prennent pas en charge l'ensemble d'instructions SSSE3. Dans de
	tels cas, elle offre de meilleures performances.

	Ce format est similaire mais plus rapide que celui utilisé par ZFS RAIDZ3.
	Comme ZFS, il ne fonctionne pas au-delà de la triple parité.

	Lors de l'utilisation de '3-parity', vous serez averti s'il est
	recommandé d'utiliser le format 'z-parity' pour une amélioration des
	performances.

	Il est possible de convertir d'un format à l'autre en ajustant
	le fichier de configuration avec le fichier z-parity ou 3-parity souhaité
	et en utilisant 'fix' pour le recréer.

  content FICHIER
	Définit le fichier à utiliser pour stocker la liste et les sommes de
	contrôle de tous les fichiers présents dans votre baie de disques.

	Il peut être placé sur un disque utilisé pour les données, la parité,
	ou tout autre disque disponible.
	Si vous utilisez un disque de données, ce fichier est automatiquement
	exclu du processus `sync`.

	Cette option est obligatoire et peut être utilisée plusieurs fois pour
	enregistrer plusieurs copies du même fichier.

	Vous devez stocker au moins une copie pour chaque disque de parité
	utilisé plus un. L'utilisation de copies supplémentaires ne nuit pas.

  data NOM DOSSIER
	Définit le nom et le point de montage des disques de données dans
	la baie. NOM est utilisé pour identifier le disque et doit
	être unique. DOSSIER est le point de montage du disque dans le
	système de fichiers.

	Vous pouvez modifier le point de montage au besoin, tant que
	vous gardez le NOM fixe.

	Vous devriez utiliser une option pour chaque disque de données
	de la baie.

	Vous pouvez renommer un disque plus tard en changeant le NOM directement
	dans le fichier de configuration, puis en exécutant une commande 'sync'.
	Dans le cas d'un renommage, l'association est faite en utilisant l'UUID
	stocké des disques.

  nohidden
	Exclut tous les fichiers et répertoires cachés.
	Sous Unix, les fichiers cachés sont ceux commençant par `.`.
	Sous Windows, ce sont ceux avec l'attribut caché.

  exclude/include MOTIF
	Définit les motifs de fichiers ou de répertoires à exclure ou à inclure
	dans le processus de synchronisation.
	Tous les motifs sont traités dans l'ordre spécifié.

	Si le premier motif qui correspond est un `exclude` (exclure), le fichier
	est exclu. S'il s'agit d'un `include` (inclure), le fichier est inclus.
	Si aucun motif ne correspond, le fichier est exclu si le dernier motif
	spécifié est un `include`, ou inclus si le dernier motif
	spécifié est un `exclude`.

	Voir la section MOTIF pour plus de détails sur les
	spécifications de motifs.

	Cette option peut être utilisée plusieurs fois.

  blocksize TAILLE_EN_KIBIOCTETS
	Définit la taille de bloc de base en kibioctets pour la parité.
	Un kibioctet est de 1024 octets.

	La taille de bloc par défaut est de 256, ce qui devrait fonctionner
	dans la plupart des cas.

	AVERTISSEMENT ! Cette option est réservée aux experts, et il est
	fortement recommandé de ne pas modifier cette valeur. Pour modifier
	cette valeur à l'avenir, vous devrez recréer l'intégralité de la parité !

	Une raison d'utiliser une taille de bloc différente est si vous avez
	beaucoup de petits fichiers, de l'ordre de millions.

	Pour chaque fichier, même s'il ne contient que quelques octets, un bloc
	entier de parité est alloué, et avec de nombreux fichiers, cela peut
	entraîner un espace de parité inutilisé important.
	Lorsque vous remplissez complètement le disque de parité, vous n'êtes
	pas autorisé à ajouter plus de fichiers aux disques de données.
	Cependant, la parité gaspillée ne s'accumule pas sur les disques de
	données. L'espace gaspillé résultant d'un grand nombre de fichiers
	sur un disque de données limite uniquement la quantité de données
	sur ce disque de données, pas les autres.

	À titre d'approximation, vous pouvez supposer que la moitié de la
	taille de bloc est gaspillée pour chaque fichier. Par exemple, avec
	100 000 fichiers et une taille de bloc de 256 Kio, vous gaspillerez
	12,8 Go de parité, ce qui peut entraîner 12,8 Go d'espace en moins
	disponible sur le disque de données.

	Vous pouvez vérifier la quantité d'espace gaspillé sur chaque disque
	en utilisant `status`.
	C'est la quantité d'espace que vous devez laisser libre sur les disques
	de données ou utiliser pour les fichiers non inclus dans la baie.
	Si cette valeur est négative, cela signifie que vous êtes près de
	remplir la parité, et cela représente l'espace que vous pouvez encore
	gaspiller.

	Pour éviter ce problème, vous pouvez utiliser une partition plus grande
	pour la parité. Par exemple, si la partition de parité est 12,8 Go plus
	grande que les disques de données, vous avez suffisamment d'espace
	supplémentaire pour gérer jusqu'à 100 000 fichiers sur chaque disque
	de données sans aucun espace gaspillé.

	Une astuce pour obtenir une partition de parité plus grande sous Linux
	est de la formater avec la commande :

		:mkfs.ext4 -m 0 -T largefile4 DEVICE

	Cela se traduit par environ 1,5 % d'espace supplémentaire, environ 60 Go
	pour un disque de 4 To, ce qui permet environ 460 000 fichiers sur
	chaque disque de données sans aucun espace gaspillé.

  hashsize TAILLE_EN_OCTETS
	Définit la taille du hachage en octets pour les blocs enregistrés.

	La taille de hachage par défaut est de 16 octets (128 bits),
	ce qui devrait fonctionner dans la plupart des cas.

	AVERTISSEMENT ! Cette option est réservée aux experts, et il est
	fortement recommandé de ne pas modifier cette valeur. Pour modifier
	cette valeur à l'avenir, vous devrez recréer l'intégralité de la parité !

	Une raison d'utiliser une taille de hachage différente est si votre
	système a une mémoire limitée. En règle générale, SnapRAID nécessite
	généralement 1 Gio de RAM pour chaque 16 To de données dans la baie.

	Plus précisément, pour stocker les hachages des données, SnapRAID
	nécessite environ TS*(1+HS)/BS octets de RAM,
	où TS est la taille totale en octets de votre baie de disques, BS est la
	taille de bloc en octets, et HS est la taille de hachage en octets.

	Par exemple, avec 8 disques de 4 To, une taille de bloc de 256 Kio
	(1 Kio = 1024 octets) et une taille de hachage de 16, vous obtenez :

	:RAM = (8 * 4 * 10^12) * (1+16) / (256 * 2^10) = 1,93 Gio

	En passant à une taille de hachage de 8, vous obtenez :

	:RAM = (8 * 4 * 10^12) * (1+8) / (256 * 2^10) = 1,02 Gio

	En passant à une taille de bloc de 512, vous obtenez :

	:RAM = (8 * 4 * 10^12) * (1+16) / (512 * 2^10) = 0,96 Gio

	En passant à la fois à une taille de hachage de 8 et à une taille de
	bloc de 512, vous obtenez :

	:RAM = (8 * 4 * 10^12) * (1+8) / (512 * 2^10) = 0,51 Gio

  autosave TAILLE_EN_GIGAOCTETS
	Enregistre automatiquement l'état lors de la synchronisation ou du
	nettoyage après la quantité spécifiée de Go traités.
	Cette option est utile pour éviter de redémarrer de longues commandes
	`sync` à partir de zéro si elles sont interrompues par un plantage
	de la machine ou tout autre événement.

  temp_limit TEMPÉRATURE_CELSIUS
	Définit la température maximale autorisée du disque en Celsius.
	Lorsqu'elle est spécifiée, SnapRAID vérifie périodiquement la
	température de tous les disques à l'aide de l'outil smartctl.
	Les températures actuelles des disques sont affichées pendant que
	SnapRAID fonctionne. Si un disque dépasse cette limite, toutes les
	opérations s'arrêtent et les disques sont arrêtés (mis en veille)
	pendant la durée définie par l'option `temp_sleep`. Après la période
	de veille, les opérations reprennent, pouvant s'arrêter à nouveau si
	la limite de température est de nouveau atteinte.

	Pendant le fonctionnement, SnapRAID analyse également la courbe de
	chauffe de chaque disque et estime la température stable à long terme
	qu'ils sont censés atteindre si l'activité continue. L'estimation
	n'est effectuée qu'après que la température du disque a augmenté
	quatre fois, garantissant que suffisamment de points de données sont
	disponibles pour établir une tendance fiable.
	Cette température stable prédite est affichée entre parenthèses à
	côté de la valeur actuelle et aide à évaluer si le refroidissement
	du système est adéquat. Cette température estimée est uniquement à
	titre informatif et n'a aucun effet sur le comportement de SnapRAID.
	Les actions du programme sont basées uniquement sur les températures
	réelles mesurées des disques.

	Pour effectuer cette analyse, SnapRAID a besoin d'une référence pour
	la température du système. Il tente d'abord de la lire à partir des
	capteurs matériels disponibles. Si aucun capteur système n'est
	accessible, il utilise la température de disque la plus basse mesurée
	au début de l'exécution comme référence de secours.

	Normalement, SnapRAID n'affiche que la température du disque le plus chaud.
	Pour afficher la température de tous les disques, utilisez l'option
	-A ou --stats.

  temp_sleep TEMPS_EN_MINUTES
	Définit le temps de veille, en minutes, lorsque la limite de température
	est atteinte. Pendant cette période, les disques restent arrêtés.
	La valeur par défaut est de 5 minutes.

  pool DOSSIER
	Définit le répertoire de mise en commun où la vue virtuelle de la
	baie de disques est créée à l'aide de la commande `pool`.

	Le répertoire doit déjà exister.

  share CHEMIN_UNC
	Définit le chemin UNC Windows requis pour accéder aux disques à distance.

	Si cette option est spécifiée, les liens symboliques créés dans le
	répertoire du pool utilisent ce chemin UNC pour accéder aux disques.
	Sans cette option, les liens symboliques générés utilisent uniquement
	des chemins locaux, ce qui ne permet pas de partager le répertoire
	du pool sur le réseau.

	Les liens symboliques sont formés en utilisant le chemin UNC spécifié,
	en ajoutant le nom du disque tel que spécifié dans l'option `data`,
	et en ajoutant enfin le répertoire et le nom du fichier.

	Cette option est requise uniquement pour Windows.

  smartctl DISQUE/PARITÉ OPTIONS...
	Définit les options smartctl personnalisées pour obtenir les attributs
	SMART de chaque disque. Cela peut être nécessaire pour les contrôleurs
	RAID et certains disques USB qui ne peuvent pas être détectés
	automatiquement. Le placeholder %s est remplacé par le nom du
	périphérique, mais il est facultatif pour les périphériques fixes
	comme les contrôleurs RAID.

	DISQUE est le même nom de disque spécifié dans l'option `data`.
	PARITÉ est l'un des noms de parité : `parity`, `2-parity`, `3-parity`,
	`4-parity`, `5-parity`, `6-parity` ou `z-parity`.

	Dans les OPTIONS spécifiées, la chaîne `%s` est remplacée par le
	nom du périphérique. Pour les contrôleurs RAID, le périphérique est
	probablement fixe, et vous n'aurez peut-être pas besoin d'utiliser `%s`.

	Consultez la documentation smartmontools pour les options possibles :

		:https://www.smartmontools.org/wiki/Supported_RAID-Controllers
		:https://www.smartmontools.org/wiki/Supported_USB-Devices

	Par exemple :

		:smartctl parity -d sat %s

  smartignore DISQUE/PARITÉ ATTR [ATTR...]
	Ignore l'attribut SMART spécifié lors du calcul de la probabilité
	de défaillance du disque. Cette option est utile si un disque signale
	des valeurs inhabituelles ou trompeuses pour un attribut particulier.

	DISQUE est le même nom de disque spécifié dans l'option `data`.
	PARITÉ est l'un des noms de parité : `parity`, `2-parity`, `3-parity`,
	`4-parity`, `5-parity`, `6-parity` ou `z-parity`.
	La valeur spéciale * peut être utilisée pour ignorer l'attribut
	sur tous les disques.

	Par exemple, pour ignorer l'attribut `Current Pending Sector Count`
	sur tous les disques :

		:smartignore * 197

	Pour l'ignorer uniquement sur le premier disque de parité :

		:smartignore parity 197

  Exemples
	Un exemple de configuration typique pour Unix est :

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

	Un exemple de configuration typique pour Windows est :

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

Motif (Pattern)
	Les motifs offrent un moyen flexible de filtrer les fichiers à inclure ou
	à exclure. En utilisant des caractères de "globbing", vous pouvez définir des règles qui
	correspondent à des noms de fichiers spécifiques ou à des structures de répertoires entières sans
	lister chaque chemin manuellement.

	Le point d'interrogation `?` correspond à n'importe quel caractère unique, sauf le
	séparateur de répertoire. Cela le rend utile pour faire correspondre des noms de fichiers avec des
	caractères variables tout en limitant le motif à un seul niveau de répertoire.

	L'étoile simple `*` correspond à n'importe quelle séquence de caractères, mais comme le
	point d'interrogation, elle ne traverse jamais les frontières des répertoires. Elle s'arrête à la
	barre oblique, ce qui la rend adaptée à la recherche de correspondances au sein d'un seul
	composant de chemin. Il s'agit du comportement standard des jokers familier du
	"globbing" shell.

	La double étoile `**` est plus puissante, elle correspond à n'importe quelle séquence de
	caractères, y compris les séparateurs de répertoires. Cela permet aux motifs de correspondre
	sur plusieurs niveaux de répertoires. Lorsque `**` apparaît directement intégré dans
	un motif, il peut correspondre à zéro ou plusieurs caractères, y compris les barres obliques entre
	le texte littéral environnant.

	L'utilisation la plus importante de `**` est sous la forme spéciale `/**/`. Celle-ci correspond à
	zéro ou plusieurs niveaux de répertoires complets, ce qui permet de faire correspondre des fichiers
	à n'importe quelle profondeur dans une arborescence de répertoires sans connaître la structure exacte du chemin.
	Par exemple, le motif `src/**/main.js` correspond à `src/main.js` (saut de
	zéro répertoire), `src/ui/main.js` (saut d'un répertoire), et
	`src/ui/components/main.js` (saut de deux répertoires).

	Les classes de caractères utilisant des crochets `[]` correspondent à un seul caractère d'un
	ensemble ou d'une plage spécifiés. Comme les autres motifs à caractère unique, elles
	ne correspondent pas aux séparateurs de répertoires. Les classes supportent les plages et la négation à l'aide
	d'un point d'exclamation.

	La distinction fondamentale à retenir est que `*`, `?` et les classes de caractères
	respectent tous les limites des répertoires et ne correspondent qu'au sein d'un seul
	composant de chemin, alors que `**` est le seul motif qui peut correspondre à travers
	les séparateurs de répertoires.

	Il existe quatre types de motifs différents :

	=FILE
		Sélectionne n'importe quel fichier nommé FILE.
		Ce motif s'applique uniquement aux fichiers, pas aux répertoires.

	=DIR/
		Sélectionne n'importe quel répertoire nommé DIR et tout son contenu.
		Ce motif s'applique uniquement aux répertoires, pas aux fichiers.

	=/PATH/FILE
		Sélectionne le chemin exact du fichier spécifié. Ce motif s'applique
		uniquement aux fichiers, pas aux répertoires.

	=/PATH/DIR/
		Sélectionne le chemin exact du répertoire spécifié et tout son contenu.
		Ce motif s'applique uniquement aux répertoires, pas aux fichiers.

	Lorsque vous spécifiez un chemin absolu commençant par /, il est
	appliqué à la racine de la baie, et non à la racine du système
	de fichiers local.

	Sous Windows, vous pouvez utiliser la barre oblique inverse \ au lieu
	de la barre oblique /.
	Les répertoires système Windows, les jonctions, les points de montage
	et les autres répertoires spéciaux Windows sont traités comme des fichiers,
	ce qui signifie que pour les exclure, vous devez utiliser une règle
	de fichier, pas une règle de répertoire.

	Si le nom de fichier contient un caractère '*', '?', '[',
	ou ']', vous devez l'échapper pour éviter qu'il ne soit interprété
	comme un caractère de globbing. Sous Unix, le caractère d'échappement
	est '\' ; sous Windows, c'est '^'.
	Lorsque le motif est sur la ligne de commande, vous devez doubler
	le caractère d'échappement pour éviter qu'il ne soit interprété
	par le shell de commande.

	Dans le fichier de configuration, vous pouvez utiliser différentes
	stratégies pour filtrer les fichiers à traiter.
	L'approche la plus simple consiste à n'utiliser que des règles
	`exclude` pour supprimer tous les fichiers et répertoires que vous
	ne voulez pas traiter. Par exemple :

		:# Exclut tout fichier nommé `*.unrecoverable`
		:exclude *.unrecoverable
		:# Exclut le répertoire racine `/lost+found`
		:exclude /lost+found/
		:# Exclut tout sous-répertoire nommé `tmp`
		:exclude tmp/

	L'approche opposée consiste à ne définir que les fichiers que vous
	souhaitez traiter, en n'utilisant que des règles `include`. Par exemple :

		:# Inclut uniquement certains répertoires
		:include /movies/
		:include /musics/
		:include /pictures/

	L'approche finale consiste à mélanger les règles `exclude` et `include`.
	Dans ce cas, l'ordre des règles est important. Les règles antérieures
	ont préséance sur les règles ultérieures.
	Pour simplifier, vous pouvez lister toutes les règles `exclude`
	d'abord, puis toutes les règles `include`. Par exemple :

		:# Exclut tout fichier nommé `*.unrecoverable`
		:exclude *.unrecoverable
		:# Exclut tout sous-répertoire nommé `tmp`
		:exclude tmp/
		:# Inclut uniquement certains répertoires
		:include /movies/
		:include /musics/
		:include /pictures/

	Sur la ligne de commande, en utilisant l'option -f, vous ne pouvez
	utiliser que des motifs `include`. Par exemple :

		:# Vérifie uniquement les fichiers .mp3.
		:# Sous Unix, utilisez des guillemets pour éviter l'expansion
		:# du globbing par le shell.
		:snapraid -f "*.mp3" check

	Sous Unix, lorsque vous utilisez des caractères de globbing sur la
	ligne de commande, vous devez les mettre entre guillemets pour empêcher
	le shell de les développer.

Ignorer des Fichiers (Ignore Files)
	En plus des règles globales du fichier de configuration, vous pouvez
	placer des fichiers `.snapraidignore` dans n'importe quel répertoire de la grappe
	pour définir des règles d'exclusion décentralisées.

	Les règles définies dans `.snapraidignore` sont appliquées après les règles du
	fichier de configuration. Cela signifie qu'elles ont une priorité plus élevée et peuvent
	être utilisées pour exclure des fichiers qui étaient précédemment inclus par la
	configuration globale. En pratique, si une règle locale correspond, le fichier est
	exclu quels que soient les paramètres d'inclusion globaux.

	La logique des motifs dans `.snapraidignore` reflète la configuration globale
	mais ancre les motifs au répertoire où se trouve le fichier :

	=FILE
		Sélectionne n'importe quel fichier nommé FILE dans ce répertoire ou en dessous.
		Cela suit les mêmes règles de globbing que le motif global.

	=DIR/
		Sélectionne n'importe quel répertoire nommé DIR et tout son contenu,
		résidant dans ce répertoire ou en dessous.

	=/PATH/FILE
		Sélectionne le fichier exact spécifié par rapport à l'emplacement
		du fichier `.snapraidignore`.

	=/PATH/DIR/
		Sélectionne le répertoire exact spécifié et tout son contenu,
		par rapport à l'emplacement du fichier `.snapraidignore`.

	Contrairement à la configuration globale, les fichiers `.snapraidignore` ne prennent
	en charge que les règles d'exclusion ; vous ne pouvez pas utiliser de motifs `include` ou de négation (!).

	Par exemple, si vous avez un `.snapraidignore` dans `/mnt/disk1/projects/` :

		:# Exclut UNIQUEMENT /mnt/disk1/projects/output.bin
		:/output.bin
		:# Exclut tout répertoire nommé 'build' dans projects/
		:build/
		:# Exclut tout fichier .tmp dans projects/ ou ses sous-dossiers
		:*.tmp

Contenu (Content)
	SnapRAID stocke la liste et les sommes de contrôle de vos fichiers
	dans le fichier de contenu.

	Il s'agit d'un fichier binaire qui répertorie tous les fichiers
	présents dans votre baie de disques, ainsi que toutes les sommes
	de contrôle pour vérifier leur intégrité.

	Ce fichier est lu et écrit par les commandes `sync` et `scrub` et
	lu par les commandes `fix`, `check` et `status`.

Parité (Parity)
	SnapRAID stocke les informations de parité de votre baie dans
	les fichiers de parité.

	Ce sont des fichiers binaires contenant la parité calculée de tous les
	blocs définis dans le fichier `content`.

	Ces fichiers sont lus et écrits par les commandes `sync` et `fix` et
	lus uniquement par les commandes `scrub` et `check`.

Encodage
	SnapRAID sous Unix ignore tout encodage. Il lit et stocke les
	noms de fichiers avec le même encodage utilisé par le système
	de fichiers.

	Sous Windows, tous les noms lus à partir du système de fichiers sont
	convertis et traités au format UTF-8.

	Pour que les noms de fichiers soient imprimés correctement, vous devez
	définir la console Windows en mode UTF-8 avec la commande `chcp 65001`
	et utiliser une police TrueType comme `Lucida Console` comme police
	de console.
	Cela n'affecte que les noms de fichiers imprimés ; si vous
	redirigez la sortie de la console vers un fichier, le fichier
	résultant est toujours au format UTF-8.

Droits d'auteur (Copyright)
	Ce fichier est Copyright (C) 2025 Andrea Mazzoleni

Voir aussi (See Also)
	snapraid_log(1), snapraidd(1)

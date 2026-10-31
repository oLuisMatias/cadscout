#include "Translate.h"

#include <QLocale>
#include <string_view>
#include <unordered_map>

namespace {

Language g_language = Language::English;

// English text as written in the code -> Portuguese (Portugal), German, Spanish, Slovak.
// An empty translation falls back to English. Keys ending in "|few" are the Slovak form for 2-4 (see plural8).
struct Entry {
    const char* en;
    const char* pt;
    const char* de;
    const char* es;
    const char* sk;
};

const Entry kTable[] = {
    // ---- Main window: buttons, tooltips, menus
    {"Open", "Abrir", "Öffnen", "Abrir", "Otvoriť"},
    {"Open a 3D model (Ctrl+O)", "Abrir um modelo 3D (Ctrl+O)", "3D-Modell öffnen (Strg+O)", "Abrir un modelo 3D (Ctrl+O)",
     "Otvoriť 3D model (Ctrl+O)"},
    {"Open File", "Abrir ficheiro", "Datei öffnen", "Abrir archivo", "Otvoriť súbor"},
    {"Show/hide assembly tree (T)", "Mostrar/ocultar a árvore da montagem (T)", "Baugruppenstruktur ein/aus (T)",
     "Mostrar/ocultar el árbol de ensamblaje (T)", "Zobraziť/skryť strom zostavy (T)"},
    {"Fit", "Ajustar", "Einpassen", "Ajustar", "Prispôsobiť"},
    {"Fit to view (F)", "Ajustar à vista (F)", "In Ansicht einpassen (F)", "Ajustar a la vista (F)", "Prispôsobiť zobrazeniu (F)"},
    {"Edges", "Arestas", "Kanten", "Aristas", "Hrany"},
    {"Show edges (E)", "Mostrar arestas (E)", "Kanten anzeigen (E)", "Mostrar aristas (E)", "Zobraziť hrany (E)"},
    {"Show Edges", "Mostrar arestas", "Kanten anzeigen", "Mostrar aristas", "Zobraziť hrany"},
    {"Settings", "Definições", "Einstellungen", "Configuración", "Nastavenia"},
    {"About", "Acerca de", "Info", "Acerca de", "O programe"},
    {"About CADscout (F1)", "Acerca do CADscout (F1)", "Über CADscout (F1)", "Acerca de CADscout (F1)", "O programe CADscout (F1)"},
    {"Open in Full", "Abrir no Full", "In Full öffnen", "Abrir en Full", "Otvoriť vo Full"},
    {"Open Full Edition (coming soon)", "Abrir edição Full (em breve)", "Full-Edition öffnen (bald verfügbar)", "Abrir edición Full (próximamente)", "Otvoriť edíciu Full (čoskoro)"},
    {"Home", "Início", "Start", "Inicio", "Domov"},
    {"Isometric View", "Vista isométrica", "Isometrische Ansicht", "Vista isométrica", "Izometrický pohľad"},
    {"Isometric view, fit to screen (H)", "Vista isométrica, ajustada ao ecrã (H)",
     "Isometrische Ansicht, an den Bildschirm angepasst (H)", "Vista isométrica, ajustada a la pantalla (H)",
     "Izometrický pohľad, prispôsobený obrazovke (H)"},
    {"Perspective On/Off", "Perspetiva ligada/desligada", "Perspektive ein/aus", "Perspectiva sí/no", "Perspektíva zap./vyp."},
    {"Snapshot", "Captura de imagem", "Schnappschuss", "Captura", "Snímka"},
    {"Cancel", "Cancelar", "Abbrechen", "Cancelar", "Zrušiť"},
    {"Stop loading (Esc)", "Parar o carregamento (Esc)", "Laden abbrechen (Esc)", "Detener la carga (Esc)", "Zastaviť načítanie (Esc)"},
    {"Ready", "Pronto", "Bereit", "Listo", "Pripravené"},
    {"Filament %1", "Filamento %1", "Filament %1", "Filamento %1", "Filament %1"},
    {"colour not set", "cor não definida", "Farbe nicht festgelegt", "color no definido", "farba nenastavená"},
    {"Color not set", "Cor não definida", "Farbe nicht festgelegt", "Color no definido", "Farba nenastavená"},
    {"not available (file is not sliced)", "não disponível (o ficheiro não foi fatiado)", "nicht verfügbar (Datei ist nicht gesliced)",
     "no disponible (el archivo no está laminado)", "nedostupné (súbor nie je slicovaný)"},
    {"Not available (file is not sliced)", "Não disponível (o ficheiro não foi fatiado)", "Nicht verfügbar (Datei ist nicht gesliced)", "No disponible (el archivo no está laminado)", "Nedostupné (súbor nie je slicovaný)"},
    {"Plate", "Placa", "Platte", "Placa", "Podložka"},
    {"Print time: %1", "Tempo de impressão: %1", "Druckzeit: %1", "Tiempo de impresión: %1", "Čas tlače: %1"},
    {"Project information (I)", "Informação do projeto (I)", "Projektinformationen (I)", "Información del proyecto (I)",
     "Informácie o projekte (I)"},
    {"Drop a 3D model here\nor press Ctrl+O", "Largue aqui um modelo 3D\nou prima Ctrl+O", "3D-Modell hierher ziehen\noder Strg+O drücken",
     "Suelta aquí un modelo 3D\no pulsa Ctrl+O", "Pretiahnite sem 3D model\nalebo stlačte Ctrl+O"},
    {"Recent Files", "Ficheiros recentes", "Zuletzt geöffnet", "Archivos recientes", "Posledné súbory"},
    {"Filename Menu", "Menu do ficheiro", "Dateimenü", "Menú del archivo", "Ponuka súboru"},
    {"This format (%1) has only one object, so there is no assembly tree.",
     "Este formato (%1) tem só um objeto, por isso não há árvore da montagem.",
     "Dieses Format (%1) enthält nur ein Objekt, daher gibt es keine Baugruppenstruktur.",
     "Este formato (%1) solo tiene un objeto, así que no hay árbol de ensamblaje.",
     "Tento formát (%1) má iba jeden objekt, preto nemá strom zostavy."},
    {"Minimize", "Minimizar", "Minimieren", "Minimizar", "Minimalizovať"},
    {"Maximize", "Maximizar", "Maximieren", "Maximizar", "Maximalizovať"},
    {"Restore", "Restaurar", "Wiederherstellen", "Restaurar", "Obnoviť"},
    {"Close", "Fechar", "Schließen", "Cerrar", "Zavrieť"},
    {"Still loading %1 - please wait until it has finished.", "Ainda a carregar %1 - aguarde até terminar.",
     "%1 wird noch geladen - bitte warten, bis der Vorgang abgeschlossen ist.", "Todavía se está cargando %1; espera a que termine.",
     "%1 sa ešte načítava - počkajte, kým sa to dokončí."},
    {"Open 3D model", "Abrir modelo 3D", "3D-Modell öffnen", "Abrir modelo 3D", "Otvoriť 3D model"},
    {"Open 3D Model", "Abrir modelo 3D", "3D-Modell öffnen", "Abrir modelo 3D", "Otvoriť 3D model"},
    {"3D models", "Modelos 3D", "3D-Modelle", "Modelos 3D", "3D modely"},
    {"3D Models", "Modelos 3D", "3D-Modelle", "Modelos 3D", "3D modely"},
    {"All files", "Todos os ficheiros", "Alle Dateien", "Todos los archivos", "Všetky súbory"},
    {"All Files", "Todos os ficheiros", "Alle Dateien", "Todos los archivos", "Všetky súbory"},
    {"Open a model first, then copy its view.", "Abra primeiro um modelo e depois copie a vista.",
     "Zuerst ein Modell öffnen, dann die Ansicht kopieren.", "Abre primero un modelo y después copia su vista.",
     "Najprv otvorte model a potom skopírujte jeho pohľad."},
    {"Could not copy the picture to the clipboard.", "Não foi possível copiar a imagem para a área de transferência.",
     "Das Bild konnte nicht in die Zwischenablage kopiert werden.", "No se pudo copiar la imagen al portapapeles.",
     "Obrázok sa nepodarilo skopírovať do schránky."},
    {"View copied as a picture (%1 × %2, transparent background)", "Vista copiada como imagem (%1 × %2, fundo transparente)",
     "Ansicht als Bild kopiert (%1 × %2, transparenter Hintergrund)", "Vista copiada como imagen (%1 × %2, fondo transparente)",
     "Pohľad skopírovaný ako obrázok (%1 × %2, priehľadné pozadie)"},
    {"Reload File", "Recarregar ficheiro", "Datei neu laden", "Recargar archivo", "Znova načítať súbor"},
    {"Show in Folder", "Mostrar na pasta", "Im Ordner anzeigen", "Mostrar en la carpeta", "Zobraziť v priečinku"},
    {"Copy File Path", "Copiar caminho do ficheiro", "Dateipfad kopieren", "Copiar ruta del archivo", "Kopírovať cestu k súboru"},
    {"File path copied", "Caminho do ficheiro copiado", "Dateipfad kopiert", "Ruta del archivo copiada", "Cesta k súboru skopírovaná"},
    {"Open in New Window", "Abrir numa nova janela", "In neuem Fenster öffnen", "Abrir en una ventana nueva", "Otvoriť v novom okne"},
    {"The new window could not be started.", "Não foi possível abrir a nova janela.", "Das neue Fenster konnte nicht gestartet werden.",
     "No se pudo abrir la ventana nueva.", "Nové okno sa nepodarilo spustiť."},
    {"Hide", "Ocultar", "Ausblenden", "Ocultar", "Skryť"},
    {"Hidden: %1. Right-click and choose \"Show All\" to bring it back.",
     "Oculto: %1. Clique com o botão direito e escolha \"Mostrar todas\" para o voltar a ver.",
     "Ausgeblendet: %1. Mit Rechtsklick und „Alle anzeigen“ wieder einblenden.",
     "Oculto: %1. Haz clic derecho y elige \"Mostrar todas\" para volver a verlo.",
     "Skryté: %1. Kliknite pravým tlačidlom a zvoľte „Zobraziť všetky“, aby sa vrátil."},
    {"Show Only", "Mostrar só esta", "Nur dieses anzeigen", "Mostrar solo esta", "Zobraziť iba tento"},
    {"ZIP Files", "Ficheiros ZIP", "ZIP-Dateien", "Archivos ZIP", "Súbory ZIP"},
    {"This zip file holds no model file that CADscout can open.",
     "Este ficheiro zip não contém nenhum modelo que o CADscout consiga abrir.",
     "Diese ZIP-Datei enthält kein Modell, das CADscout öffnen kann.",
     "Este archivo zip no contiene ningún modelo que CADscout pueda abrir.",
     "Tento súbor zip neobsahuje žiadny model, ktorý dokáže CADscout otvoriť."},
    {"Open which model from %1?", "Que modelo abrir de %1?", "Welches Modell aus %1 öffnen?", "¿Qué modelo abrir de %1?",
     "Ktorý model otvoriť z %1?"},
    {"The zip file could not be read.", "Não foi possível ler o ficheiro zip.", "Die ZIP-Datei konnte nicht gelesen werden.",
     "No se pudo leer el archivo zip.", "Súbor zip sa nepodarilo prečítať."},
    {"Zip files larger than 4 GB are not supported.", "Ficheiros zip com mais de 4 GB não são suportados.",
     "ZIP-Dateien über 4 GB werden nicht unterstützt.", "Los archivos zip de más de 4 GB no son compatibles.",
     "Súbory zip väčšie ako 4 GB nie sú podporované."},
    {"This zip file is protected with a password.", "Este ficheiro zip está protegido por palavra-passe.",
     "Diese ZIP-Datei ist mit einem Passwort geschützt.", "Este archivo zip está protegido con contraseña.",
     "Tento súbor zip je chránený heslom."},
    {"The zip file uses a compression method that is not supported.",
     "O ficheiro zip usa um método de compressão que não é suportado.",
     "Die ZIP-Datei verwendet ein nicht unterstütztes Komprimierungsverfahren.",
     "El archivo zip usa un método de compresión que no es compatible.",
     "Súbor zip používa nepodporovanú metódu kompresie."},
    {"The model is no longer in the zip file.", "O modelo já não está no ficheiro zip.",
     "Das Modell ist nicht mehr in der ZIP-Datei.", "El modelo ya no está en el archivo zip.", "Model už nie je v súbore zip."},
    {"The model could not be unpacked from the zip file.", "Não foi possível extrair o modelo do ficheiro zip.",
     "Das Modell konnte nicht aus der ZIP-Datei entpackt werden.", "No se pudo extraer el modelo del archivo zip.",
     "Model sa nepodarilo rozbaliť zo súboru zip."},
    {"Make Transparent", "Tornar transparente", "Transparent machen", "Hacer transparente", "Spraviť priehľadným"},
    {"Make Solid", "Tornar sólida", "Undurchsichtig machen", "Hacer sólida", "Spraviť plným"},
    {"Transparent parts cannot be clicked: the parts behind them can. Right-click and choose \"Make All Solid\" to undo.",
     "As peças transparentes não podem ser clicadas, as que estão atrás delas sim. Clique com o botão direito e escolha "
     "\"Tornar todas sólidas\" para anular.",
     "Transparente Teile lassen sich nicht anklicken, die Teile dahinter schon. Mit Rechtsklick und „Alle undurchsichtig "
     "machen“ rückgängig machen.",
     "Las piezas transparentes no se pueden seleccionar, las de detrás sí. Haz clic derecho y elige \"Hacer todas "
     "sólidas\" para deshacerlo.",
     "Na priehľadné diely sa nedá kliknúť, na diely za nimi áno. Vrátite to pravým kliknutím a voľbou „Spraviť všetky "
     "plnými“."},
    {"Make All Solid (%1 transparent)", "Tornar todas sólidas (%1 transparentes)",
     "Alle undurchsichtig machen (%1 transparent)", "Hacer todas sólidas (%1 transparentes)",
     "Spraviť všetky plnými (priehľadné: %1)"},
    {"Show All (%1 hidden)", "Mostrar todas (%1 ocultas)", "Alle anzeigen (%1 ausgeblendet)",
     "Mostrar todas (%1 ocultas)", "Zobraziť všetky (skryté: %1)"},
    {"Show All", "Mostrar todas", "Alle anzeigen", "Mostrar todas", "Zobraziť všetky"},
    {"Reading the file", "A ler o ficheiro", "Datei wird gelesen", "Leyendo el archivo", "Čítanie súboru"},
    {"Translating the model", "A converter o modelo", "Modell wird umgewandelt", "Convirtiendo el modelo", "Prevod modelu"},
    {"Repairing the parts", "A reparar as peças", "Teile werden repariert", "Reparando las piezas", "Oprava dielov"},
    {"Meshing the surfaces", "A criar a malha das superfícies", "Oberflächen werden vernetzt", "Mallando las superficies",
     "Tvorba siete plôch"},
    {"Preparing the view", "A preparar a vista", "Ansicht wird vorbereitet", "Preparando la vista", "Príprava zobrazenia"},
    {"Cancelling...", "A cancelar...", "Wird abgebrochen...", "Cancelando...", "Ruší sa..."},
    {"No Recent Files", "Sem ficheiros recentes", "Keine zuletzt geöffneten Dateien", "No hay archivos recientes", "Žiadne posledné súbory"},
    {"Clear List", "Limpar lista", "Liste leeren", "Borrar lista", "Vymazať zoznam"},
    {"This file no longer exists:\n%1", "Este ficheiro já não existe:\n%1", "Diese Datei existiert nicht mehr:\n%1",
     "Este archivo ya no existe:\n%1", "Tento súbor už neexistuje:\n%1"},
    {"CADscout Full is coming soon", "O CADscout Full chega em breve", "CADscout Full kommt bald", "CADscout Full llegará pronto",
     "CADscout Full bude čoskoro"},
    {"CADscout Full will be available soon. Once it is released, install it to open this model in it.",
     "O CADscout Full estará disponível em breve. Quando for lançado, instale-o para abrir este modelo nele.",
     "CADscout Full ist bald verfügbar. Sobald es erschienen ist, installieren Sie es, um dieses Modell darin zu öffnen.",
     "CADscout Full estará disponible pronto. Cuando se publique, instálalo para abrir este modelo en él.",
     "CADscout Full bude čoskoro k dispozícii. Keď vyjde, nainštalujte ho a otvorte v ňom tento model."},
    {"Unable to open Full edition", "Não foi possível abrir a edição Full", "Full-Edition kann nicht geöffnet werden",
     "No se puede abrir la edición Full", "Edíciu Full nie je možné otvoriť"},
    {"The Full edition was found but could not be started.", "A edição Full foi encontrada, mas não foi possível iniciá-la.",
     "Die Full-Edition wurde gefunden, konnte aber nicht gestartet werden.", "Se encontró la edición Full, pero no se pudo iniciar.",
     "Edícia Full sa našla, ale nepodarilo sa ju spustiť."},
    {"Loading %1 ...", "A carregar %1 ...", "%1 wird geladen ...", "Cargando %1 ...", "Načítava sa %1 ..."},
    {"Unknown error while reading the file.", "Erro desconhecido ao ler o ficheiro.", "Unbekannter Fehler beim Lesen der Datei.",
     "Error desconocido al leer el archivo.", "Neznáma chyba pri čítaní súboru."},
    {"Loading cancelled", "Carregamento cancelado", "Laden abgebrochen", "Carga cancelada", "Načítanie zrušené"},
    {"Loading cancelled - %1", "Carregamento cancelado - %1", "Laden abgebrochen - %1", "Carga cancelada - %1", "Načítanie zrušené - %1"},
    {"Could not reload %1 - still showing the previous version", "Não foi possível recarregar %1 - continua a mostrar a versão anterior",
     "%1 konnte nicht neu geladen werden - die vorherige Version wird weiter angezeigt",
     "No se pudo recargar %1; se sigue mostrando la versión anterior", "%1 sa nepodarilo znova načítať - zobrazuje sa predchádzajúca verzia"},
    {"Failed to load %1", "Falha ao carregar %1", "%1 konnte nicht geladen werden", "No se pudo cargar %1", "%1 sa nepodarilo načítať"},
    {"Loaded in %1 s", "Carregado em %1 s", "Geladen in %1 s", "Cargado en %1 s", "Načítané za %1 s"},
    {"Loaded in %1 s (from cache)", "Carregado em %1 s (da cache)", "Geladen in %1 s (aus dem Cache)", "Cargado en %1 s (de la caché)",
     "Načítané za %1 s (z vyrovnávacej pamäte)"},
    {"%1 faces", "%1 faces", "%1 Flächen", "%1 caras", "%1 plôch"},
    {"%1 triangles", "%1 triângulos", "%1 Dreiecke", "%1 triángulos", "%1 trojuholníkov"},

    // ---- Settings window
    {"Command", "Comando", "Befehl", "Comando", "Príkaz"},
    {"Keybinding", "Atalho", "Tastenbelegung", "Atajo", "Skratka"},
    {"When", "Quando", "Wann", "Cuándo", "Kedy"},
    {"Source", "Origem", "Quelle", "Origen", "Zdroj"},
    {"Search settings", "Procurar definições", "Einstellungen durchsuchen", "Buscar en la configuración", "Hľadať v nastaveniach"},
    {"Support", "Apoiar", "Unterstützen", "Apoyar", "Podpora"},
    {"General", "Geral", "Allgemein", "General", "Všeobecné"},
    {"Controls", "Controlos", "Steuerung", "Controles", "Ovládanie"},
    {"Shortcuts", "Atalhos", "Tastenkürzel", "Atajos", "Klávesové skratky"},
    {"Mesh quality", "Qualidade da malha", "Netzqualität", "Calidad de malla", "Kvalita siete"},
    {"Print plate", "Placa de impressão", "Druckplatte", "Placa de impresión", "Tlačová podložka"},
    {"Cache", "Cache", "Cache", "Caché", "Vyrovnávacia pamäť"},
    {"No settings found.", "Nenhuma definição encontrada.", "Keine Einstellungen gefunden.", "No se encontró ninguna opción.",
     "Nenašli sa žiadne nastavenia."},
    {"Search results", "Resultados da pesquisa", "Suchergebnisse", "Resultados de la búsqueda", "Výsledky hľadania"},
    {"CADscout is free software. If it is useful to you, you can support its development with a donation.\n\n"
     "You pay on the donation website, in your browser. CADscout never sees your payment details, and nothing is unlocked in return.",
     "O CADscout é software livre. Se lhe for útil, pode apoiar o seu desenvolvimento com um donativo.\n\n"
     "O pagamento é feito no site de donativos, no seu navegador. O CADscout nunca vê os seus dados de pagamento e nada é "
     "desbloqueado em troca.",
     "CADscout ist freie Software. Wenn es Ihnen nützt, können Sie die Entwicklung mit einer Spende unterstützen.\n\n"
     "Sie bezahlen auf der Spendenseite in Ihrem Browser. CADscout sieht Ihre Zahlungsdaten nie, und im Gegenzug wird nichts "
     "freigeschaltet.",
     "CADscout es software libre. Si te resulta útil, puedes apoyar su desarrollo con un donativo.\n\n"
     "El pago se hace en la web de donativos, en tu navegador. CADscout nunca ve tus datos de pago y no se desbloquea nada a cambio.",
     "CADscout je slobodný softvér. Ak je pre vás užitočný, môžete jeho vývoj podporiť darom.\n\n"
     "Platíte na stránke pre dary vo svojom prehliadači. CADscout nikdy nevidí vaše platobné údaje a nič sa tým neodomkne."},
    {"Left / Right arrow keys open the previous / next model in the same folder",
     "As setas Esquerda / Direita abrem o modelo anterior / seguinte na mesma pasta",
     "Pfeiltasten Links / Rechts öffnen das vorherige / nächste Modell im selben Ordner",
     "Las flechas izquierda / derecha abren el modelo anterior / siguiente de la misma carpeta",
     "Šípky doľava / doprava otvoria predchádzajúci / ďalší model v tom istom priečinku"},
    {"Browse files", "Percorrer ficheiros", "Dateien durchblättern", "Recorrer archivos", "Prechádzanie súborov"},
    {"Turn this off if you do not want the arrow keys to change the model.", "Desligue se não quiser que as setas mudem o modelo.",
     "Ausschalten, wenn die Pfeiltasten das Modell nicht wechseln sollen.",
     "Desactívalo si no quieres que las flechas cambien el modelo.", "Vypnite, ak nechcete, aby šípky menili model."},
    {"Reload the model when its file changes", "Recarregar o modelo quando o ficheiro mudar",
     "Modell neu laden, wenn sich die Datei ändert", "Recargar el modelo cuando cambie su archivo",
     "Znova načítať model, keď sa jeho súbor zmení"},
    {"Reload", "Recarregar", "Neu laden", "Recargar", "Opätovné načítanie"},
    {"Useful when you export a model from a CAD or slicer program and keep CADscout open: the view updates by itself a moment "
     "after the file has been saved, and the camera stays where it is. F5 reloads by hand.",
     "Útil quando exporta um modelo de um programa CAD ou de fatiamento e mantém o CADscout aberto: a vista atualiza-se sozinha "
     "pouco depois de o ficheiro ser guardado, e a câmara fica onde está. F5 recarrega manualmente.",
     "Praktisch, wenn Sie ein Modell aus einem CAD- oder Slicer-Programm exportieren und CADscout offen lassen: Die Ansicht "
     "aktualisiert sich kurz nach dem Speichern der Datei von selbst, und die Kamera bleibt, wo sie ist. F5 lädt von Hand neu.",
     "Útil cuando exportas un modelo desde un programa CAD o laminador y mantienes CADscout abierto: la vista se actualiza sola "
     "poco después de guardar el archivo y la cámara se queda donde está. F5 recarga a mano.",
     "Užitočné, keď exportujete model z CAD programu alebo slicera a CADscout necháte otvorený: zobrazenie sa samo aktualizuje "
     "chvíľu po uložení súboru a kamera zostane na mieste. F5 načíta znova ručne."},
    {"Millimetres (mm)", "Milímetros (mm)", "Millimeter (mm)", "Milímetros (mm)", "Milimetre (mm)"},
    {"Inches (in)", "Polegadas (in)", "Zoll (in)", "Pulgadas (in)", "Palce (in)"},
    {"Size units", "Unidades de tamanho", "Größeneinheit", "Unidades de tamaño", "Jednotky rozmerov"},
    {"The unit of the sizes in the status bar. Inches only apply to files whose unit is known to be millimetres "
     "(STEP, IGES, BREP, STL, 3MF); other files show their numbers as they are.",
     "A unidade dos tamanhos na barra de estado. As polegadas só se aplicam a ficheiros cuja unidade se sabe ser milímetros "
     "(STEP, IGES, BREP, STL, 3MF); os outros ficheiros mostram os números tal como estão.",
     "Die Einheit der Größen in der Statusleiste. Zoll gilt nur für Dateien, deren Einheit bekanntermaßen Millimeter ist "
     "(STEP, IGES, BREP, STL, 3MF); andere Dateien zeigen ihre Zahlen unverändert.",
     "La unidad de los tamaños en la barra de estado. Las pulgadas solo se aplican a archivos cuya unidad se sabe que es el "
     "milímetro (STEP, IGES, BREP, STL, 3MF); los demás archivos muestran sus números tal cual.",
     "Jednotka rozmerov v stavovom riadku. Palce platia iba pre súbory, ktorých jednotkou sú zaručene milimetre "
     "(STEP, IGES, BREP, STL, 3MF); ostatné súbory zobrazujú čísla tak, ako sú."},
    {"Language", "Idioma", "Sprache", "Idioma", "Jazyk"},
    {"The language of CADscout. It changes the next time CADscout starts.",
     "O idioma do CADscout. Muda na próxima vez que o CADscout for iniciado.",
     "Die Sprache von CADscout. Sie ändert sich beim nächsten Start von CADscout.",
     "El idioma de CADscout. Cambia la próxima vez que se inicie CADscout.",
     "Jazyk programu CADscout. Zmení sa pri ďalšom spustení programu CADscout."},
    {"Automatic (Windows language)", "Automático (idioma do Windows)", "Automatisch (Windows-Sprache)",
     "Automático (idioma de Windows)", "Automaticky (jazyk Windows)"},
    {"The new language is used the next time CADscout starts.", "O novo idioma é usado na próxima vez que o CADscout for iniciado.",
     "Die neue Sprache wird beim nächsten Start von CADscout verwendet.",
     "El nuevo idioma se usará la próxima vez que se inicie CADscout.", "Nový jazyk sa použije pri ďalšom spustení programu CADscout."},
    {"CAD (middle = orbit)", "CAD (meio = rodar)", "CAD (Mitte = Drehen)", "CAD (central = órbita)", "CAD (stredné = otáčanie)"},
    {"Classic (left = orbit)", "Clássico (esquerdo = rodar)", "Klassisch (links = Drehen)", "Clásico (izquierdo = órbita)",
     "Klasické (ľavé = otáčanie)"},
    {"Preset", "Predefinição", "Vorlage", "Predefinido", "Predvoľba"},
    {"Start from a ready-made mouse layout, then change single bindings in the table below.",
     "Comece com uma disposição pronta do rato e depois altere ações individuais na tabela abaixo.",
     "Mit einer fertigen Mausbelegung beginnen und dann einzelne Belegungen in der Tabelle unten ändern.",
     "Empieza con una distribución del ratón ya hecha y luego cambia acciones concretas en la tabla de abajo.",
     "Začnite pripraveným rozložením myši a potom zmeňte jednotlivé priradenia v tabuľke nižšie."},
    {"Left", "Esquerdo", "Links", "Izquierdo", "Ľavé"},
    {"Middle", "Meio", "Mitte", "Central", "Stredné"},
    {"Right", "Direito", "Rechts", "Derecho", "Pravé"},
    {"None", "Nenhuma", "Keine", "Ninguna", "Žiadny"},
    {"Orbit", "Rodar", "Drehen", "Órbita", "Otáčanie"},
    {"Pan", "Deslocar", "Verschieben", "Desplazar", "Posun"},
    {"Zoom (drag)", "Zoom (arrastar)", "Zoom (ziehen)", "Zoom (arrastrar)", "Priblíženie (ťahanie)"},
    {"3D view", "Vista 3D", "3D-Ansicht", "Vista 3D", "3D zobrazenie"},
    {"Default", "Predefinido", "Standard", "Predeterminado", "Predvolené"},
    {"Custom", "Personalizado", "Benutzerdefiniert", "Personalizado", "Vlastné"},
    {"Built-in", "Integrado", "Integriert", "Integrado", "Vstavané"},
    {"Zoom (wheel)", "Zoom (roda)", "Zoom (Mausrad)", "Zoom (rueda)", "Priblíženie (koliesko)"},
    {"Mouse Wheel", "Roda do rato", "Mausrad", "Rueda del ratón", "Koliesko myši"},
    {"Select part / plate", "Selecionar peça / placa", "Teil / Platte auswählen", "Seleccionar pieza / placa", "Vybrať diel / podložku"},
    {"Left Button", "Botão esquerdo", "Linke Taste", "Botón izquierdo", "Ľavé tlačidlo"},
    {"Middle Button", "Botão do meio", "Mittlere Taste", "Botón central", "Stredné tlačidlo"},
    {"Right Button", "Botão direito", "Rechte Taste", "Botón derecho", "Pravé tlačidlo"},
    {"Mouse controls of the 3D view. Click the pencil to change a binding.",
     "Controlos do rato na vista 3D. Clique no lápis para alterar uma ação.",
     "Maussteuerung der 3D-Ansicht. Auf den Stift klicken, um eine Belegung zu ändern.",
     "Controles del ratón en la vista 3D. Haz clic en el lápiz para cambiar una acción.",
     "Ovládanie myšou v 3D zobrazení. Kliknutím na ceruzku zmeníte priradenie."},
    {"Reverse the direction of mouse wheel zoom", "Inverter a direção do zoom com a roda do rato", "Zoomrichtung des Mausrads umkehren",
     "Invertir la dirección del zoom con la rueda del ratón", "Obrátiť smer priblíženia kolieskom myši"},
    {"Invert wheel", "Inverter roda", "Mausrad umkehren", "Invertir rueda", "Obrátiť koliesko"},
    {"Open model", "Abrir modelo", "Modell öffnen", "Abrir modelo", "Otvoriť model"},
    {"Anywhere", "Em qualquer lado", "Überall", "En cualquier lugar", "Kdekoľvek"},
    {"Fit to view", "Ajustar à vista", "In Ansicht einpassen", "Ajustar a la vista", "Prispôsobiť zobrazeniu"},
    {"Next file in folder", "Ficheiro seguinte na pasta", "Nächste Datei im Ordner", "Siguiente archivo de la carpeta",
     "Ďalší súbor v priečinku"},
    {"Model loaded", "Modelo carregado", "Modell geladen", "Modelo cargado", "Načítaný model"},
    {"Previous file in folder", "Ficheiro anterior na pasta", "Vorherige Datei im Ordner", "Archivo anterior de la carpeta",
     "Predchádzajúci súbor v priečinku"},
    {"Reload file", "Recarregar ficheiro", "Datei neu laden", "Recargar archivo", "Znova načítať súbor"},
    {"Open in new window", "Abrir numa nova janela", "In neuem Fenster öffnen", "Abrir en una ventana nueva", "Otvoriť v novom okne"},
    {"Cancel loading", "Cancelar o carregamento", "Laden abbrechen", "Cancelar la carga", "Zrušiť načítanie"},
    {"While a file loads", "Durante o carregamento", "Während eine Datei lädt", "Mientras se carga un archivo", "Počas načítania súboru"},
    {"Hide a part, make it transparent", "Ocultar uma peça, torná-la transparente", "Teil ausblenden, transparent machen",
     "Ocultar una pieza, hacerla transparente", "Skryť diel, spraviť ho priehľadným"},
    {"Click on a part", "Clique numa peça", "Klick auf ein Teil", "Clic en una pieza", "Kliknutie na diel"},
    {"Show / hide edges", "Mostrar / ocultar arestas", "Kanten ein / aus", "Mostrar / ocultar aristas", "Zobraziť / skryť hrany"},
    {"Home view", "Vista inicial", "Startansicht", "Vista inicial", "Úvodný pohľad"},
    {"Show / hide assembly tree", "Mostrar / ocultar árvore da montagem", "Baugruppenstruktur ein / aus",
     "Mostrar / ocultar árbol de ensamblaje", "Zobraziť / skryť strom zostavy"},
    {"Show / hide project information", "Mostrar / ocultar informação do projeto", "Projektinformationen ein / aus",
     "Mostrar / ocultar información del proyecto", "Zobraziť / skryť informácie o projekte"},
    {"3MF project loaded", "Projeto 3MF carregado", "3MF-Projekt geladen", "Proyecto 3MF cargado", "Načítaný projekt 3MF"},
    {"About CADscout", "Acerca do CADscout", "Über CADscout", "Acerca de CADscout", "O programe CADscout"},
    {"Close the info popup", "Fechar a janela de informação", "Infofenster schließen", "Cerrar la ventana de información",
     "Zavrieť informačné okno"},
    {"Keyboard shortcuts of the viewer. They are fixed and cannot be changed.",
     "Atalhos de teclado do visualizador. São fixos e não podem ser alterados.",
     "Tastenkürzel des Viewers. Sie sind fest und können nicht geändert werden.",
     "Atajos de teclado del visor. Son fijos y no se pueden cambiar.",
     "Klávesové skratky prehliadača. Sú pevné a nedajú sa zmeniť."},
    {"Change binding: %1", "Alterar ação: %1", "Belegung ändern: %1", "Cambiar acción: %1", "Zmeniť priradenie: %1"},
    {"Mouse button", "Botão do rato", "Maustaste", "Botón del ratón", "Tlačidlo myši"},
    {"Modifier key", "Tecla modificadora", "Zusatztaste", "Tecla modificadora", "Modifikačný kláves"},
    {"Fast", "Rápida", "Schnell", "Rápida", "Rýchla"},
    {"Balanced", "Equilibrada", "Ausgewogen", "Equilibrada", "Vyvážená"},
    {"High quality", "Alta qualidade", "Hohe Qualität", "Alta calidad", "Vysoká kvalita"},
    {"Quality", "Qualidade", "Qualität", "Calidad", "Kvalita"},
    {"Fast: coarser curves, lowest memory and quickest loading. Balanced: good looking curves at a reasonable load time. "
     "High quality: very smooth curves, slower loading and more memory. Applies to files opened after saving.",
     "Rápida: curvas mais grosseiras, menos memória e carregamento mais rápido. Equilibrada: curvas com bom aspeto num tempo de "
     "carregamento razoável. Alta qualidade: curvas muito suaves, carregamento mais lento e mais memória. Aplica-se aos ficheiros "
     "abertos depois de guardar.",
     "Schnell: gröbere Rundungen, wenig Speicher und schnellstes Laden. Ausgewogen: schöne Rundungen bei vernünftiger Ladezeit. "
     "Hohe Qualität: sehr glatte Rundungen, langsameres Laden und mehr Speicher. Gilt für Dateien, die nach dem Speichern geöffnet "
     "werden.",
     "Rápida: curvas más bastas, menos memoria y carga más rápida. Equilibrada: curvas con buen aspecto con un tiempo de carga "
     "razonable. Alta calidad: curvas muy suaves, carga más lenta y más memoria. Se aplica a los archivos abiertos después de guardar.",
     "Rýchla: hrubšie krivky, najmenej pamäte a najrýchlejšie načítanie. Vyvážená: pekné krivky pri rozumnom čase načítania. "
     "Vysoká kvalita: veľmi hladké krivky, pomalšie načítanie a viac pamäte. Platí pre súbory otvorené po uložení."},
    {"Remember big models so they open faster next time", "Lembrar modelos grandes para que abram mais depressa da próxima vez",
     "Große Modelle merken, damit sie beim nächsten Mal schneller öffnen",
     "Recordar los modelos grandes para que se abran más rápido la próxima vez",
     "Pamätať si veľké modely, aby sa nabudúce otvorili rýchlejšie"},
    {"Remember models", "Lembrar modelos", "Modelle merken", "Recordar modelos", "Pamätať si modely"},
    {"Models that take a while to load are saved in a folder of this app on your computer. When you open the same, unchanged file "
     "again it opens in a moment. The saved data never leaves your computer, and it is deleted when you uninstall the app.",
     "Os modelos que demoram a carregar são guardados numa pasta desta aplicação no seu computador. Quando voltar a abrir o mesmo "
     "ficheiro sem alterações, ele abre num instante. Os dados guardados nunca saem do seu computador e são apagados quando "
     "desinstalar a aplicação.",
     "Modelle, deren Laden etwas dauert, werden in einem Ordner dieser App auf Ihrem Computer gespeichert. Wenn Sie dieselbe, "
     "unveränderte Datei erneut öffnen, ist sie sofort da. Die gespeicherten Daten verlassen Ihren Computer nie und werden beim "
     "Deinstallieren der App gelöscht.",
     "Los modelos que tardan en cargarse se guardan en una carpeta de esta aplicación en tu ordenador. Cuando vuelves a abrir el "
     "mismo archivo sin cambios, se abre al instante. Los datos guardados nunca salen de tu ordenador y se borran al desinstalar la "
     "aplicación.",
     "Modely, ktorých načítanie trvá dlhšie, sa ukladajú do priečinka tejto aplikácie vo vašom počítači. Keď ten istý nezmenený "
     "súbor otvoríte znova, otvorí sa okamžite. Uložené údaje nikdy neopustia váš počítač a pri odinštalovaní aplikácie sa vymažú."},
    {"1 second", "1 segundo", "1 Sekunde", "1 segundo", "1 sekunda"},
    {"%1 seconds", "%1 segundos", "%1 Sekunden", "%1 segundos", "%1 sekúnd"},
    {"%1 seconds|few", "", "", "", "%1 sekundy"},
    {"%1 opens when the current loading has finished.", "%1 abre quando o carregamento atual terminar.",
     "%1 wird geöffnet, sobald das aktuelle Laden fertig ist.", "%1 se abrirá cuando termine la carga actual.",
     "%1 sa otvorí po dokončení aktuálneho načítania."},
    {"1 texture/material file could not be unpacked", "1 ficheiro de textura/material não pôde ser extraído",
     "1 Textur-/Materialdatei konnte nicht entpackt werden", "No se pudo extraer 1 archivo de textura/material",
     "1 súbor textúry/materiálu sa nepodarilo rozbaliť"},
    {"%1 texture/material files could not be unpacked", "%1 ficheiros de textura/material não puderam ser extraídos",
     "%1 Textur-/Materialdateien konnten nicht entpackt werden", "No se pudieron extraer %1 archivos de textura/material",
     "%1 súborov textúr/materiálov sa nepodarilo rozbaliť"},
    {"%1 texture/material files could not be unpacked|few", "", "", "",
     "%1 súbory textúr/materiálov sa nepodarilo rozbaliť"},
    {"Minimum load time", "Tempo mínimo de carregamento", "Minimale Ladezeit", "Tiempo mínimo de carga", "Minimálny čas načítania"},
    {"A model is only saved when opening it took at least this long. Models that open faster are not worth saving, and leaving "
     "them out keeps the saved data small.",
     "Um modelo só é guardado se a abertura demorou pelo menos este tempo. Não vale a pena guardar modelos que abrem mais depressa, "
     "e deixá-los de fora mantém os dados guardados pequenos.",
     "Ein Modell wird nur gespeichert, wenn das Öffnen mindestens so lange gedauert hat. Schneller öffnende Modelle lohnen sich "
     "nicht, und ohne sie bleiben die gespeicherten Daten klein.",
     "Un modelo solo se guarda si abrirlo tardó al menos este tiempo. No merece la pena guardar los modelos que se abren más rápido, "
     "y dejarlos fuera mantiene pequeños los datos guardados.",
     "Model sa uloží, len ak jeho otvorenie trvalo aspoň takto dlho. Modely, ktoré sa otvoria rýchlejšie, sa neoplatí ukladať a "
     "vďaka tomu zostanú uložené údaje malé."},
    {"Size limit", "Limite de tamanho", "Größenlimit", "Límite de tamaño", "Limit veľkosti"},
    {"When the saved models use more space than this, the ones you opened longest ago are deleted first.",
     "Quando os modelos guardados ocuparem mais espaço do que isto, os que abriu há mais tempo são apagados primeiro.",
     "Wenn die gespeicherten Modelle mehr Platz brauchen, werden zuerst die gelöscht, die Sie am längsten nicht geöffnet haben.",
     "Cuando los modelos guardados ocupan más espacio que esto, se borran primero los que abriste hace más tiempo.",
     "Keď uložené modely zaberú viac miesta, najprv sa vymažú tie, ktoré ste otvorili najdávnejšie."},
    {"Clear cache now", "Limpar a cache agora", "Cache jetzt leeren", "Vaciar la caché ahora", "Vymazať vyrovnávaciu pamäť"},
    {"Open cache folder", "Abrir a pasta da cache", "Cache-Ordner öffnen", "Abrir la carpeta de la caché",
     "Otvoriť priečinok vyrovnávacej pamäte"},
    {"The cache folder could not be opened.", "Não foi possível abrir a pasta da cache.",
     "Der Cache-Ordner konnte nicht geöffnet werden.", "No se pudo abrir la carpeta de la caché.",
     "Priečinok vyrovnávacej pamäte sa nepodarilo otvoriť."},
    {"Used now: %1 MB", "Usado agora: %1 MB", "Jetzt belegt: %1 MB", "Usado ahora: %1 MB", "Teraz využité: %1 MB"},
    {"Saved models", "Modelos guardados", "Gespeicherte Modelle", "Modelos guardados", "Uložené modely"},
    {"This takes effect at once, without pressing Save.", "Tem efeito imediato, sem premir Guardar.",
     "Wirkt sofort, ohne auf Speichern zu klicken.", "Se aplica al momento, sin pulsar Guardar.",
     "Prejaví sa hneď, bez stlačenia tlačidla Uložiť."},
    {"Automatic (from the file)", "Automático (do ficheiro)", "Automatisch (aus der Datei)", "Automático (del archivo)",
     "Automaticky (zo súboru)"},
    {"Custom size", "Tamanho personalizado", "Eigene Größe", "Tamaño personalizado", "Vlastná veľkosť"},
    {"Hidden", "Oculta", "Ausgeblendet", "Oculta", "Skrytá"},
    {"Automatic: Bambu Studio and Orca Slicer files show the printer bed stored in the file, one per plate; other files show no "
     "plate. Custom size: always show one plate of the size below, centred under the model. Hidden: never show a plate.",
     "Automático: os ficheiros do Bambu Studio e do Orca Slicer mostram a base da impressora guardada no ficheiro, uma por placa; "
     "os outros ficheiros não mostram placa. Tamanho personalizado: mostra sempre uma placa com o tamanho abaixo, centrada debaixo "
     "do modelo. Oculta: nunca mostra uma placa.",
     "Automatisch: Dateien aus Bambu Studio und Orca Slicer zeigen das in der Datei gespeicherte Druckbett, eines pro Platte; "
     "andere Dateien zeigen keine Platte. Eigene Größe: immer eine Platte der unten angegebenen Größe, mittig unter dem Modell. "
     "Ausgeblendet: nie eine Platte anzeigen.",
     "Automático: los archivos de Bambu Studio y Orca Slicer muestran la cama de la impresora guardada en el archivo, una por "
     "placa; los demás archivos no muestran placa. Tamaño personalizado: muestra siempre una placa del tamaño de abajo, centrada "
     "debajo del modelo. Oculta: nunca muestra una placa.",
     "Automaticky: súbory z Bambu Studio a Orca Slicer zobrazia podložku tlačiarne uloženú v súbore, jednu pre každú platňu; "
     "ostatné súbory podložku nezobrazia. Vlastná veľkosť: vždy zobrazí jednu podložku s veľkosťou nižšie, vycentrovanú pod "
     "modelom. Skrytá: podložku nikdy nezobrazí."},
    {"Width", "Largura", "Breite", "Ancho", "Šírka"},
    {"Depth", "Profundidade", "Tiefe", "Fondo", "Hĺbka"},
    {"Plate width (X) and depth (Y) in millimetres. Used when Plate is set to Custom size.",
     "Largura (X) e profundidade (Y) da placa em milímetros. Usado quando Placa está em Tamanho personalizado.",
     "Breite (X) und Tiefe (Y) der Platte in Millimetern. Gilt, wenn Platte auf Eigene Größe steht.",
     "Ancho (X) y fondo (Y) de la placa en milímetros. Se usa cuando Placa está en Tamaño personalizado.",
     "Šírka (X) a hĺbka (Y) podložky v milimetroch. Použije sa, keď je Podložka nastavená na Vlastnú veľkosť."},
    {"Conflict", "Conflito", "Konflikt", "Conflicto", "Konflikt"},
    {"Two actions use the same button and modifier.", "Duas ações usam o mesmo botão e a mesma tecla modificadora.",
     "Zwei Aktionen verwenden dieselbe Taste und Zusatztaste.", "Dos acciones usan el mismo botón y la misma tecla modificadora.",
     "Dve akcie používajú to isté tlačidlo a modifikačný kláves."},
    {"Save", "Guardar", "Speichern", "Guardar", "Uložiť"},
    {"OK", "OK", "OK", "Aceptar", "OK"},

    // ---- About window, donation buttons, messages
    {"Version %1  ·  CAD and 3D print file viewer", "Versão %1  ·  Visualizador de ficheiros CAD e de impressão 3D",
     "Version %1  ·  Viewer für CAD- und 3D-Druck-Dateien", "Versión %1  ·  Visor de archivos CAD y de impresión 3D",
     "Verzia %1  ·  Prehliadač CAD súborov a súborov pre 3D tlač"},
    {"Free software under the MIT license. Copyright (c) 2026 CADscout contributors.\n"
     "The license text is in the file LICENSE next to the program.",
     "Software livre sob a licença MIT. Copyright (c) 2026 CADscout contributors.\n"
     "O texto da licença está no ficheiro LICENSE junto do programa.",
     "Freie Software unter der MIT-Lizenz. Copyright (c) 2026 CADscout contributors.\n"
     "Der Lizenztext steht in der Datei LICENSE neben dem Programm.",
     "Software libre bajo la licencia MIT. Copyright (c) 2026 CADscout contributors.\n"
     "El texto de la licencia está en el archivo LICENSE junto al programa.",
     "Slobodný softvér pod licenciou MIT. Copyright (c) 2026 CADscout contributors.\n"
     "Text licencie je v súbore LICENSE vedľa programu."},
    {"<b>Early version.</b> CADscout has not been tested with many files yet. If a file does not open correctly, or you have an "
     "idea, please %1.",
     "<b>Versão inicial.</b> O CADscout ainda não foi testado com muitos ficheiros. Se um ficheiro não abrir corretamente, ou se "
     "tiver uma ideia, por favor %1.",
     "<b>Frühe Version.</b> CADscout wurde noch nicht mit vielen Dateien getestet. Wenn eine Datei nicht richtig geöffnet wird "
     "oder Sie eine Idee haben, %1.",
     "<b>Versión temprana.</b> CADscout aún no se ha probado con muchos archivos. Si un archivo no se abre bien, o tienes una idea, "
     "por favor %1.",
     "<b>Skorá verzia.</b> CADscout ešte nebol otestovaný s mnohými súbormi. Ak sa súbor neotvorí správne alebo máte nápad, "
     "prosím, %1."},
    {"leave a review in the Microsoft Store", "deixe uma avaliação na Microsoft Store",
     "schreiben Sie bitte eine Bewertung im Microsoft Store", "deja una reseña en Microsoft Store",
     "napíšte recenziu v obchode Microsoft Store"},
    {"Support CADscout (it is free; nothing is unlocked in return):", "Apoie o CADscout (é gratuito; nada é desbloqueado em troca):",
     "CADscout unterstützen (es ist kostenlos; im Gegenzug wird nichts freigeschaltet):",
     "Apoya CADscout (es gratuito; no se desbloquea nada a cambio):", "Podporte CADscout (je zadarmo; nič sa tým neodomkne):"},
    {"CADscout is built with Qt (LGPL v3), Open CASCADE Technology (LGPL 2.1 with exception) and other open-source libraries. "
     "Their licenses:",
     "O CADscout é feito com Qt (LGPL v3), Open CASCADE Technology (LGPL 2.1 com exceção) e outras bibliotecas de código aberto. "
     "As suas licenças:",
     "CADscout basiert auf Qt (LGPL v3), Open CASCADE Technology (LGPL 2.1 mit Ausnahme) und weiteren Open-Source-Bibliotheken. "
     "Deren Lizenzen:",
     "CADscout está hecho con Qt (LGPL v3), Open CASCADE Technology (LGPL 2.1 con excepción) y otras bibliotecas de código "
     "abierto. Sus licencias:",
     "CADscout je postavený na Qt (LGPL v3), Open CASCADE Technology (LGPL 2.1 s výnimkou) a ďalších open-source knižniciach. "
     "Ich licencie:"},
    {"This donation page is not set up yet.", "Esta página de donativos ainda não está configurada.",
     "Diese Spendenseite ist noch nicht eingerichtet.", "Esta página de donativos aún no está configurada.",
     "Táto stránka pre dary ešte nie je nastavená."},
    {"Scan with your phone to open the %1 page", "Leia com o telemóvel para abrir a página %1",
     "Mit dem Handy scannen, um die Seite %1 zu öffnen", "Escanea con el móvil para abrir la página de %1",
     "Naskenujte telefónom a otvorte stránku %1"},
    {"Click, or scan the code with your phone.", "Clique, ou leia o código com o telemóvel.",
     "Klicken oder den Code mit dem Handy scannen.", "Haz clic o escanea el código con el móvil.",
     "Kliknite alebo naskenujte kód telefónom."},
    {"The graphics driver could not prepare the 3D view, so models cannot be shown. Please update your graphics driver.",
     "O controlador gráfico não conseguiu preparar a vista 3D, por isso os modelos não podem ser mostrados. Atualize o "
     "controlador gráfico.",
     "Der Grafiktreiber konnte die 3D-Ansicht nicht vorbereiten, daher können keine Modelle angezeigt werden. Bitte "
     "aktualisieren Sie den Grafiktreiber.",
     "El controlador gráfico no pudo preparar la vista 3D, así que no se pueden mostrar modelos. Actualiza el controlador gráfico.",
     "Grafický ovládač nedokázal pripraviť 3D zobrazenie, preto nemožno zobraziť modely. Aktualizujte grafický ovládač."},
    {"The 3D view needs a graphics card or driver that supports OpenGL 3.3, and this computer does not provide it.\n\n"
     "Please update your graphics driver and try again. In a virtual machine, turn on 3D acceleration.",
     "A vista 3D precisa de uma placa gráfica ou controlador com suporte para OpenGL 3.3, e este computador não o oferece.\n\n"
     "Atualize o controlador gráfico e tente de novo. Numa máquina virtual, ative a aceleração 3D.",
     "Die 3D-Ansicht braucht eine Grafikkarte oder einen Treiber mit OpenGL 3.3, und dieser Computer bietet das nicht.\n\n"
     "Bitte aktualisieren Sie den Grafiktreiber und versuchen Sie es erneut. In einer virtuellen Maschine die "
     "3D-Beschleunigung einschalten.",
     "La vista 3D necesita una tarjeta gráfica o un controlador compatible con OpenGL 3.3, y este ordenador no lo tiene.\n\n"
     "Actualiza el controlador gráfico y vuelve a intentarlo. En una máquina virtual, activa la aceleración 3D.",
     "3D zobrazenie potrebuje grafickú kartu alebo ovládač s podporou OpenGL 3.3, a tento počítač ju nemá.\n\n"
     "Aktualizujte grafický ovládač a skúste to znova. Vo virtuálnom počítači zapnite 3D akceleráciu."},

    // ---- Names the loader gives to parts, plates and the status bar summary
    {"Assembly", "Montagem", "Baugruppe", "Ensamblaje", "Zostava"},
    {"Scene", "Cena", "Szene", "Escena", "Scéna"},
    {"Shape %1", "Forma %1", "Form %1", "Forma %1", "Tvar %1"},
    {"Part %1", "Peça %1", "Teil %1", "Pieza %1", "Diel %1"},
    {"Object %1", "Objeto %1", "Objekt %1", "Objeto %1", "Objekt %1"},
    {"Plate %1", "Placa %1", "Platte %1", "Placa %1", "Podložka %1"},
    {" (filament %1)", " (filamento %1)", " (Filament %1)", " (filamento %1)", " (filament %1)"},
    {"%1 object", "%1 objeto", "%1 Objekt", "%1 objeto", "%1 objekt"},
    {"%1 objects", "%1 objetos", "%1 Objekte", "%1 objetos", "%1 objektov"},
    {"%1 objects|few", "", "", "", "%1 objekty"},
    {"%1 plate", "%1 placa", "%1 Platte", "%1 placa", "%1 podložka"},
    {"%1 plates", "%1 placas", "%1 Platten", "%1 placas", "%1 podložiek"},
    {"%1 plates|few", "", "", "", "%1 podložky"},
    {"%1 filament", "%1 filamento", "%1 Filament", "%1 filamento", "%1 filament"},
    {"%1 filaments", "%1 filamentos", "%1 Filamente", "%1 filamentos", "%1 filamentov"},
    {"%1 filaments|few", "", "", "", "%1 filamenty"},
    {"%1 mm nozzle", "bico de %1 mm", "%1-mm-Düse", "boquilla de %1 mm", "tryska %1 mm"},
    {"%1 more objects", "mais %1 objetos", "%1 weitere Objekte", "%1 objetos más", "ďalších %1 objektov"},
    {"%1 skipped", "%1 ignorados", "%1 übersprungen", "%1 omitidos", "vynechané: %1"},
    {"Plate %1 · filament %2", "Placa %1 · filamento %2", "Platte %1 · Filament %2", "Placa %1 · filamento %2",
     "Podložka %1 · filament %2"},
    {"Plate %1, filament %2", "Placa %1, filamento %2", "Platte %1, Filament %2", "Placa %1, filamento %2", "Podložka %1, filament %2"},

    // ---- Project information panel (3MF)
    {"Project", "Projeto", "Projekt", "Proyecto", "Projekt"},
    {"Made with", "Feito com", "Erstellt mit", "Hecho con", "Vytvorené v"},
    {"Title", "Título", "Titel", "Título", "Názov"},
    {"Designer", "Designer", "Designer", "Diseñador", "Dizajnér"},
    {"Description", "Descrição", "Beschreibung", "Descripción", "Popis"},
    {"Copyright", "Copyright", "Copyright", "Copyright", "Copyright"},
    {"Licence", "Licença", "Lizenz", "Licencia", "Licencia"},
    {"Created", "Criado", "Erstellt", "Creado", "Vytvorené"},
    {"Modified", "Modificado", "Geändert", "Modificado", "Zmenené"},
    {"Origin", "Origem", "Herkunft", "Origen", "Pôvod"},
    {"Printer", "Impressora", "Drucker", "Impresora", "Tlačiareň"},
    {"Profile", "Perfil", "Profil", "Perfil", "Profil"},
    {"Nozzle", "Bico", "Düse", "Boquilla", "Tryska"},
    {"Bed", "Base", "Druckbett", "Cama", "Podložka"},
    {"Maximum height", "Altura máxima", "Maximale Höhe", "Altura máxima", "Maximálna výška"},
    {"G-code flavour", "Tipo de G-code", "G-Code-Variante", "Tipo de G-code", "Variant G-kódu"},
    {"Print profile", "Perfil de impressão", "Druckprofil", "Perfil de impresión", "Tlačový profil"},
    {"Based on", "Baseado em", "Basiert auf", "Basado en", "Založené na"},
    {"Layer height", "Altura da camada", "Schichthöhe", "Altura de capa", "Výška vrstvy"},
    {"First layer", "Primeira camada", "Erste Schicht", "Primera capa", "Prvá vrstva"},
    {"Walls", "Paredes", "Wände", "Paredes", "Steny"},
    {"Top / bottom layers", "Camadas superiores / inferiores", "Obere / untere Schichten", "Capas superiores / inferiores",
     "Horné / spodné vrstvy"},
    {"Infill", "Enchimento", "Füllung", "Relleno", "Výplň"},
    {"Supports", "Suportes", "Stützen", "Soportes", "Podpery"},
    {"Brim", "Aba", "Rand (Brim)", "Borde (brim)", "Okraj (brim)"},
    {"Skirt loops", "Voltas de saia", "Skirt-Runden", "Vueltas de falda", "Slučky skirtu"},
    {"Raft layers", "Camadas de jangada", "Raft-Schichten", "Capas de balsa", "Vrstvy raftu"},
    {"Seam", "Costura", "Naht", "Costura", "Šev"},
    {"Ironing", "Engomar", "Glätten", "Planchado", "Žehlenie"},
    {"Fuzzy skin", "Pele difusa", "Fuzzy Skin", "Piel difusa", "Chlpatý povrch"},
    {"Spiral vase", "Vaso em espiral", "Spiralvase", "Jarrón en espiral", "Špirálová váza"},
    {"Prime tower", "Torre de purga", "Reinigungsturm", "Torre de purga", "Čistiaca veža"},
    {"Print sequence", "Sequência de impressão", "Druckreihenfolge", "Secuencia de impresión", "Poradie tlače"},
    {"Build plate", "Placa de impressão", "Druckplatte", "Placa de impresión", "Tlačová platňa"},
    {"Changed from profile", "Alterado do perfil", "Geändert gegenüber Profil", "Cambiado respecto al perfil", "Zmenené oproti profilu"},
    {"%1 settings: %2", "%1 definições: %2", "%1 Einstellungen: %2", "%1 ajustes: %2", "%1 nastavení: %2"},
    {"(%1 more)", "(mais %1)", "(%1 weitere)", "(%1 más)", "(ďalších %1)"},
    {"On", "Ligado", "Ein", "Sí", "Zap."},
    {"Off", "Desligado", "Aus", "No", "Vyp."},
    {"Filaments", "Filamentos", "Filamente", "Filamentos", "Filamenty"},
    {"Used in this project", "Usado neste projeto", "In diesem Projekt verwendet", "Usado en este proyecto", "Použitý v tomto projekte"},
    {"Not used", "Não usado", "Nicht verwendet", "No usado", "Nepoužitý"},
    {"Plates", "Placas", "Platten", "Placas", "Podložky"},
    {"Objects", "Objetos", "Objekte", "Objetos", "Objekty"},
    {"Slicing result", "Resultado do fatiamento", "Slicing-Ergebnis", "Resultado del laminado", "Výsledok slicovania"},
    {"Warning", "Aviso", "Warnung", "Advertencia", "Upozornenie"},
    {"All plates", "Todas as placas", "Alle Platten", "Todas las placas", "Všetky podložky"},
    {"Status", "Estado", "Status", "Estado", "Stav"},
    {"Not sliced. Slice the plates in the slicer and save the project to see print time and filament use here.",
     "Não fatiado. Fatie as placas no programa de fatiamento e guarde o projeto para ver aqui o tempo de impressão e o filamento usado.",
     "Nicht gesliced. Platten im Slicer slicen und das Projekt speichern, um hier Druckzeit und Filamentverbrauch zu sehen.",
     "Sin laminar. Lamina las placas en el laminador y guarda el proyecto para ver aquí el tiempo de impresión y el filamento usado.",
     "Neslicované. Slicujte podložky v sliceri a uložte projekt, aby ste tu videli čas tlače a spotrebu filamentu."},

    // ---- Error messages of the readers
    {"Could not read %1 file.", "Não foi possível ler o ficheiro %1.", "Die %1-Datei konnte nicht gelesen werden.",
     "No se pudo leer el archivo %1.", "Súbor %1 sa nepodarilo prečítať."},
    {"Could not translate %1 file.", "Não foi possível converter o ficheiro %1.", "Die %1-Datei konnte nicht umgewandelt werden.",
     "No se pudo convertir el archivo %1.", "Súbor %1 sa nepodarilo previesť."},
    {"Could not read the file.", "Não foi possível ler o ficheiro.", "Die Datei konnte nicht gelesen werden.",
     "No se pudo leer el archivo.", "Súbor sa nepodarilo prečítať."},
    {"Not a valid %1 file.", "Não é um ficheiro %1 válido.", "Keine gültige %1-Datei.", "No es un archivo %1 válido.",
     "Nie je to platný súbor %1."},
    {"The file contains no triangles.", "O ficheiro não contém triângulos.", "Die Datei enthält keine Dreiecke.",
     "El archivo no contiene triángulos.", "Súbor neobsahuje žiadne trojuholníky."},
    {"Unexpected end of PLY file.", "Fim inesperado do ficheiro PLY.", "Unerwartetes Ende der PLY-Datei.",
     "Final inesperado del archivo PLY.", "Neočakávaný koniec súboru PLY."},
    {"Unsupported PLY property type: %1", "Tipo de propriedade PLY não suportado: %1", "Nicht unterstützter PLY-Eigenschaftstyp: %1",
     "Tipo de propiedad PLY no compatible: %1", "Nepodporovaný typ vlastnosti PLY: %1"},
    {"PLY file has an invalid vertex index.", "O ficheiro PLY tem um índice de vértice inválido.",
     "Die PLY-Datei hat einen ungültigen Eckpunktindex.", "El archivo PLY tiene un índice de vértice no válido.",
     "Súbor PLY má neplatný index vrcholu."},
    {"The file contains no shapes.", "O ficheiro não contém formas.", "Die Datei enthält keine Formen.",
     "El archivo no contiene formas.", "Súbor neobsahuje žiadne tvary."},
    {"The file contains no geometry.", "O ficheiro não contém geometria.", "Die Datei enthält keine Geometrie.",
     "El archivo no contiene geometría.", "Súbor neobsahuje žiadnu geometriu."},
    {"No displayable surfaces found.", "Não foram encontradas superfícies para mostrar.", "Keine darstellbaren Flächen gefunden.",
     "No se encontraron superficies que mostrar.", "Nenašli sa žiadne zobraziteľné plochy."},
    {"SolidWorks files (.sldprt / .sldasm) can't be opened directly, because SolidWorks keeps the format closed.\n\n"
     "In SolidWorks, use File > Save As > STEP (*.step), then open that STEP file here.",
     "Os ficheiros SolidWorks (.sldprt / .sldasm) não podem ser abertos diretamente, porque o SolidWorks mantém o formato "
     "fechado.\n\nNo SolidWorks, use Ficheiro > Guardar como > STEP (*.step) e depois abra aqui esse ficheiro STEP.",
     "SolidWorks-Dateien (.sldprt / .sldasm) können nicht direkt geöffnet werden, weil SolidWorks das Format geschlossen hält."
     "\n\nIn SolidWorks Datei > Speichern unter > STEP (*.step) wählen und dann diese STEP-Datei hier öffnen.",
     "Los archivos de SolidWorks (.sldprt / .sldasm) no se pueden abrir directamente, porque SolidWorks mantiene el formato "
     "cerrado.\n\nEn SolidWorks, usa Archivo > Guardar como > STEP (*.step) y luego abre aquí ese archivo STEP.",
     "Súbory SolidWorks (.sldprt / .sldasm) nemožno otvoriť priamo, pretože SolidWorks drží formát uzavretý.\n\n"
     "V SolidWorks použite Súbor > Uložiť ako > STEP (*.step) a potom tu otvorte tento súbor STEP."},
    {"Unsupported file type%1. Supported: STEP, IGES, BREP, STL, OBJ, PLY, VRML, glTF/GLB, 3MF and DXF.",
     "Tipo de ficheiro não suportado%1. Suportados: STEP, IGES, BREP, STL, OBJ, PLY, VRML, glTF/GLB, 3MF e DXF.",
     "Nicht unterstützter Dateityp%1. Unterstützt: STEP, IGES, BREP, STL, OBJ, PLY, VRML, glTF/GLB, 3MF und DXF.",
     "Tipo de archivo no compatible%1. Compatibles: STEP, IGES, BREP, STL, OBJ, PLY, VRML, glTF/GLB, 3MF y DXF.",
     "Nepodporovaný typ súboru%1. Podporované: STEP, IGES, BREP, STL, OBJ, PLY, VRML, glTF/GLB, 3MF a DXF."},
    {"The 3MF file is not a valid zip archive.", "O ficheiro 3MF não é um arquivo zip válido.",
     "Die 3MF-Datei ist kein gültiges ZIP-Archiv.", "El archivo 3MF no es un archivo zip válido.", "Súbor 3MF nie je platný archív zip."},
    {"The 3MF file contains no 3D model.", "O ficheiro 3MF não contém nenhum modelo 3D.", "Die 3MF-Datei enthält kein 3D-Modell.",
     "El archivo 3MF no contiene ningún modelo 3D.", "Súbor 3MF neobsahuje žiadny 3D model."},
    {"The 3MF file refers to \"%1\", which is missing from the archive.", "O ficheiro 3MF refere \"%1\", que falta no arquivo.",
     "Die 3MF-Datei verweist auf „%1“, das im Archiv fehlt.", "El archivo 3MF hace referencia a \"%1\", que falta en el archivo.",
     "Súbor 3MF odkazuje na „%1“, ktorý v archíve chýba."},
    {"The 3MF file has an invalid vertex index.", "O ficheiro 3MF tem um índice de vértice inválido.",
     "Die 3MF-Datei hat einen ungültigen Eckpunktindex.", "El archivo 3MF tiene un índice de vértice no válido.",
     "Súbor 3MF má neplatný index vrcholu."},
    {"The 3MF file contains no meshes.", "O ficheiro 3MF não contém malhas.", "Die 3MF-Datei enthält keine Netze.",
     "El archivo 3MF no contiene mallas.", "Súbor 3MF neobsahuje žiadne siete."},
    {"Binary DXF files are not supported. Save the DXF as ASCII text.",
     "Os ficheiros DXF binários não são suportados. Guarde o DXF como texto ASCII.",
     "Binäre DXF-Dateien werden nicht unterstützt. DXF als ASCII-Text speichern.",
     "Los archivos DXF binarios no son compatibles. Guarda el DXF como texto ASCII.",
     "Binárne súbory DXF nie sú podporované. Uložte DXF ako text ASCII."},
    {"Lines", "Linhas", "Linien", "Líneas", "Čiary"},
    {"%1 per kg", "%1 por kg", "%1 pro kg", "%1 por kg", "%1 za kg"},
    {"nozzle %1 °C", "bico %1 °C", "Düse %1 °C", "boquilla %1 °C", "tryska %1 °C"},
    {"bed %1 °C", "mesa %1 °C", "Bett %1 °C", "cama %1 °C", "podložka %1 °C"},
    {"Nothing of the model is in the view, so there is nothing to copy. Press F to fit the model.",
     "Nenhuma parte do modelo está na vista, não há nada para copiar. Prima F para ajustar o modelo.",
     "Vom Modell ist nichts in der Ansicht, es gibt nichts zu kopieren. F drücken, um das Modell einzupassen.",
     "No se ve nada del modelo, no hay nada que copiar. Pulsa F para encuadrar el modelo.",
     "Z modelu nie je nič v zábere, nie je čo kopírovať. Stlačte F na prispôsobenie modelu."},
    {"All parts are hidden, so there is nothing to copy.", "Todas as peças estão ocultas, não há nada para copiar.",
     "Alle Teile sind ausgeblendet, es gibt nichts zu kopieren.", "Todas las piezas están ocultas, no hay nada que copiar.",
     "Všetky diely sú skryté, nie je čo kopírovať."},
    {"DXF mesh", "Malha DXF", "DXF-Netz", "Malla DXF", "Sieť DXF"},
    {"DXF mesh (colour %1)", "Malha DXF (cor %1)", "DXF-Netz (Farbe %1)", "Malla DXF (color %1)", "Sieť DXF (farba %1)"},
    {"No drawable geometry found in this DXF.", "Não foi encontrada geometria visualizável neste DXF.",
     "In dieser DXF-Datei wurde keine darstellbare Geometrie gefunden.", "No se encontró geometría visualizable en este DXF.",
     "V tomto súbore DXF sa nenašla žiadna zobraziteľná geometria."},
    {"Unsupported entities:", "Entidades não suportadas:", "Nicht unterstützte Elemente:", "Entidades no compatibles:",
     "Nepodporované entity:"},
};

const std::unordered_map<std::string_view, const Entry*>& index() {
    static const std::unordered_map<std::string_view, const Entry*> map = [] {  // built once, then only read
        std::unordered_map<std::string_view, const Entry*> m;
        for (const Entry& e : kTable) m.emplace(e.en, &e);
        return m;
    }();
    return map;
}

const char* lookup(const char* english, bool fallback) {
    if (g_language == Language::English) return fallback ? english : nullptr;
    const auto& map = index();
    const auto it = map.find(english);
    if (it == map.end()) return fallback ? english : nullptr;
    const Entry& e = *it->second;
    const char* t = g_language == Language::Portuguese ? e.pt
                    : g_language == Language::German   ? e.de
                    : g_language == Language::Spanish  ? e.es
                                                       : e.sk;
    if (!t || !*t) return fallback ? english : nullptr;
    return t;
}

std::string fill(std::string text, std::initializer_list<std::string> args) {
    int n = 1;
    for (const std::string& a : args) {
        const std::string mark = "%" + std::to_string(n++);
        const size_t pos = text.find(mark);
        if (pos != std::string::npos) text.replace(pos, mark.size(), a);
    }
    return text;
}

}  // namespace

void setLanguage(Language language) { g_language = language; }

Language currentLanguage() { return g_language; }

Language languageFromSetting(int setting) {
    if (setting >= 1 && setting <= 5) return Language(setting - 1);
    switch (QLocale::system().language()) {  // 0 or unknown: like Windows
    case QLocale::Portuguese: return Language::Portuguese;
    case QLocale::German: return Language::German;
    case QLocale::Spanish: return Language::Spanish;
    case QLocale::Slovak: return Language::Slovak;
    default: return Language::English;
    }
}

const char* tr8(const char* english) { return lookup(english, true); }

std::string tr8f(const char* english, std::initializer_list<std::string> args) { return fill(tr8(english), args); }

std::string plural8(long long n, const char* one, const char* many) {
    const char* key = n == 1 ? one : many;
    const char* text = tr8(key);
    if (g_language == Language::Slovak && n >= 2 && n <= 4) {  // Slovak: 2-4 have their own form
        if (const char* few = lookup((std::string(many) + "|few").c_str(), false)) text = few;
    }
    return fill(text, {std::to_string(n)});
}

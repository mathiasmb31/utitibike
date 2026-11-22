import QtQuick 2.12
import Ubuntu.Components 1.3
import "components"

// Page d'aide complète — décrit le fonctionnement global de l'app
// et l'enregistrement GPS avec le daemon.
Page {
    id: helpPage
    header: AppHeader { title: i18n.tr("Aide") }

    // Fond sombre (la Page a un fond blanc par défaut)
    Rectangle {
        anchors.fill: parent
        color: Theme.bgPage
        z: -10
    }

    Flickable {
        anchors.fill: parent
        anchors.margins: units.gu(2)
        contentHeight: helpCol.height
        clip: true

        Column {
            id: helpCol
            width: parent.width
            spacing: units.gu(2)

            // ── Titre ────────────────────────────────────────────
            Label {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: "UT WheelTrek"
                fontSize: "x-large"
                font.bold: true
                color: Theme.accentOrange
            }
            Label {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: i18n.tr("Guide d'utilisation")
                fontSize: "medium"
                color: Theme.textSecondary
            }

            // ── C'est quoi ? ─────────────────────────────────────
            HelpSection {
                title: i18n.tr("C'est quoi, UT WheelTrek ?")
                body: i18n.tr("UT WheelTrek est une application pour le vélo et la randonnée à pied. "
                            + "Elle affiche une carte, calcule des itinéraires, "
                            + "suggère des tracés à proximité, enregistre vos parcours "
                            + "et les exporte. La carte fonctionne hors ligne une fois les tuiles "
                            + "téléchargées ; le calcul d'itinéraire, la recherche de lieux et "
                            + "les suggestions nécessitent une connexion.")
            }

            // ── La carte ─────────────────────────────────────────
            HelpSection {
                title: i18n.tr("La carte")
                body: i18n.tr("• Glissez le doigt pour déplacer la carte\n"
                            + "• Pincez deux doigts pour zoomer\n"
                            + "• Appui long sur la carte : ajoute un point de passage\n"
                            + "• Bouton « Couche » (colonne de droite) : changer le style de carte\n"
                            + "• Bouton recherche : trouver une adresse ou un lieu")
            }

            // ── Calculer un itinéraire ───────────────────────────
            HelpSection {
                title: i18n.tr("Calculer un itinéraire")
                body: i18n.tr("1. Appuyez longuement sur la carte pour poser un point de départ\n"
                            + "2. Posez un ou plusieurs points de passage\n"
                            + "3. Appuyez sur le bouton orange « Itinéraire »\n"
                            + "4. Choisissez un profil :\n"
                            + "     • Randonnée (chemins tranquilles)\n"
                            + "     • Rapide (routes directes)\n"
                            + "     • Sécurisé (évite les grands axes)\n"
                            + "     • Court (distance minimale)\n"
                            + "5. L'itinéraire s'affiche sur la carte avec la distance et le dénivelé")
            }

            // ── Navigation GPS ───────────────────────────────────
            HelpSection {
                title: i18n.tr("Navigation GPS (suivi en direct)")
                body: i18n.tr("1. Activez le GPS avec le bouton « Activer GPS »\n"
                            + "2. Appuyez sur « Naviguer » (bouton bleu)\n"
                            + "3. Suivez la flèche bleue sur la carte\n"
                            + "4. L'écran reste allumé pendant la navigation\n"
                            + "5. Des vibrations vous préviennent à l'approche des virages\n"
                            + "6. Appuyez sur « Arrêter nav. » pour terminer")
            }

            // ── Enregistrement GPS (le daemon) ────────────────────
            HelpSection {
                title: i18n.tr("Enregistrer un parcours (mode économie)")
                body: i18n.tr("Le bouton « Enregistrer » lance un petit programme "
                            + "en arrière-plan (un daemon). Ce daemon :\n"
                            + "• Note votre position toutes les 5 secondes\n"
                            + "• Fait un bip toutes les 30 secondes\n"
                            + "• Continue même si vous éteignez l'écran\n"
                            + "• Continue même si vous quittez l'application\n\n"
                            + "Vous pouvez donc rouler longtemps sans vous soucier "
                            + "de la batterie. Le parcours est sauvegardé quand vous "
                            + "appuyez sur « Arrêter ».")
            }

            // ── Schéma du daemon ─────────────────────────────────
            Label {
                width: parent.width
                text: i18n.tr("Comment ça marche ?")
                fontSize: "medium"
                font.bold: true
                color: Theme.textPrimary
            }
            Rectangle {
                width: parent.width
                height: diagramCol.height + units.gu(2)
                color: Theme.bgCard
                radius: units.gu(1)

                Column {
                    id: diagramCol
                    anchors.centerIn: parent
                    width: parent.width - units.gu(4)
                    spacing: units.gu(0.3)

                    Label {
                        width: parent.width
                        wrapMode: Text.Wrap
                        fontSize: "x-small"
                        font.family: "monospace"
                        horizontalAlignment: Text.AlignHCenter
                        color: Theme.textPrimary
                        text: i18n.tr("┌──────────────┐     ┌──────────────────────┐")
                    }
                    Label {
                        width: parent.width
                        wrapMode: Text.Wrap
                        fontSize: "x-small"
                        font.family: "monospace"
                        horizontalAlignment: Text.AlignHCenter
                        color: Theme.textPrimary
                        text: i18n.tr("│ UT WheelTrek │────▶│  Daemon GPS          │")
                    }
                    Label {
                        width: parent.width
                        wrapMode: Text.Wrap
                        fontSize: "x-small"
                        font.family: "monospace"
                        horizontalAlignment: Text.AlignHCenter
                        color: Theme.textPrimary
                        text: i18n.tr("│  (l'appli)   │     │  (en arrière-plan)   │")
                    }
                    Label {
                        width: parent.width
                        wrapMode: Text.Wrap
                        fontSize: "x-small"
                        font.family: "monospace"
                        horizontalAlignment: Text.AlignHCenter
                        color: Theme.textPrimary
                        text: i18n.tr("│              │     │                      │")
                    }
                    Label {
                        width: parent.width
                        wrapMode: Text.Wrap
                        fontSize: "x-small"
                        font.family: "monospace"
                        horizontalAlignment: Text.AlignHCenter
                        color: Theme.textPrimary
                        text: i18n.tr("│ Bouton       │     │ • note la position   │")
                    }
                    Label {
                        width: parent.width
                        wrapMode: Text.Wrap
                        fontSize: "x-small"
                        font.family: "monospace"
                        horizontalAlignment: Text.AlignHCenter
                        color: Theme.textPrimary
                        text: i18n.tr("│ Enregistrer  │     │ • bip toutes les 30s │")
                    }
                    Label {
                        width: parent.width
                        wrapMode: Text.Wrap
                        fontSize: "x-small"
                        font.family: "monospace"
                        horizontalAlignment: Text.AlignHCenter
                        color: Theme.textPrimary
                        text: i18n.tr("│ Bouton       │     │ • sauvegarde le      │")
                    }
                    Label {
                        width: parent.width
                        wrapMode: Text.Wrap
                        fontSize: "x-small"
                        font.family: "monospace"
                        horizontalAlignment: Text.AlignHCenter
                        color: Theme.textPrimary
                        text: i18n.tr("│ Arrêter      │     │ • s'arrête tout seul  │")
                    }
                    Label {
                        width: parent.width
                        wrapMode: Text.Wrap
                        fontSize: "x-small"
                        font.family: "monospace"
                        horizontalAlignment: Text.AlignHCenter
                        color: Theme.textPrimary
                        text: i18n.tr("└──────────────┘     └──────────────────────┘")
                    }
                }
            }

            // ── Historique ───────────────────────────────────────
            HelpSection {
                title: i18n.tr("Historique des parcours")
                body: i18n.tr("Tous vos parcours enregistrés sont dans le panneau Historique "
                            + "(bouton dans le menu de la carte).\n"
                            + "Appuyez sur un parcours pour le recharger sur la carte : vous "
                            + "verrez alors son profil d'altitude et, pour les tracés "
                            + "enregistrés, la vitesse (moyenne, courbe colorée, bouton "
                            + "« Pente » pour comparer avec/sans correction de pente).\n"
                            + "Le filtre permet de trier par date ou par distance. Glissez "
                            + "vers la droite pour charger, vers la gauche pour supprimer.\n"
                            + "L'export et le partage GPX se font depuis le panneau Itinéraire "
                            + "une fois le tracé chargé.")
            }

            // ── Favoris ──────────────────────────────────────────
            HelpSection {
                title: i18n.tr("Favoris")
                body: i18n.tr("Appuyez sur l'étoile à côté d'un lieu pour l'ajouter "
                            + "à vos favoris. Retrouvez vos lieux préférés rapidement "
                            + "depuis le panneau latéral.")
            }

            // ── Hors ligne ───────────────────────────────────────
            HelpSection {
                title: i18n.tr("Mode hors ligne")
                body: i18n.tr("Les tuiles de carte sont mises en cache automatiquement. "
                            + "Une fois une zone téléchargée, elle reste disponible "
                            + "sans connexion internet. Idéal pour les sorties à la campagne.")
            }

            // ── Suggestions ───────────────────────────────────────
            HelpSection {
                title: i18n.tr("Tracés suggérés")
                body: i18n.tr("Le bouton « Sug » cherche des itinéraires nommés autour de vous. "
                            + "Le rayon suit la durée maximale choisie (30 km par heure, "
                            + "jusqu'à 100 km). Les résultats sont mis en cache 10 minutes : "
                            + "le bouton « Actualiser » force une nouvelle recherche et "
                            + "« Annuler » l'interrompt. Les tracés du réseau national "
                            + "(NCN) et régional (RCN) sont affichés en premier.")
            }

            // ── Profil / tuiles ───────────────────────────────────
            HelpSection {
                title: i18n.tr("Profil et tuiles")
                body: i18n.tr("Sur une trace enregistrée, le profil affiche la vitesse moyenne "
                            + "et colore la courbe : vert au-dessus de la moyenne, rouge en "
                            + "dessous. Le bouton « Pente » compare la vitesse à celle des "
                            + "segments de même pente (une montée ralentit normalement).\n"
                            + "Si une tuile semble absente ou périmée, utilisez "
                            + "« Rafraîchir les tuiles visibles » dans le panneau Itinéraire.")
            }

            // ── Batterie ──────────────────────────────────────────
            HelpSection {
                title: i18n.tr("Économie de batterie")
                body: i18n.tr("Le daemon GPS consomme peu :\n"
                            + "• Pas d'écran allumé\n"
                            + "• Un point GPS toutes les 5 secondes\n"
                            + "• Un bip léger toutes les 30 secondes\n\n"
                            + "Résultat : 8 à 12 heures d'enregistrement continu.")
            }

            // ── Sécurité ─────────────────────────────────────────
            HelpSection {
                title: i18n.tr("Sécurité")
                body: i18n.tr("Le daemon continue d'enregistrer même si vous quittez "
                            + "l'application ou si l'écran s'éteint.\n\n"
                            + "Il s'arrête automatiquement si vous appuyez sur « Arrêter », "
                            + "si la batterie descend à 20 %, après 1 h sans signal GPS "
                            + "ou sans mouvement, ou si le téléphone redémarre.")
            }

            // ── Export ───────────────────────────────────────────
            HelpSection {
                title: i18n.tr("Exporter un parcours")
                body: i18n.tr("Vos parcours sont au format GPX, compatible avec :\n"
                            + "• Strava\n"
                            + "• Garmin Connect\n"
                            + "• Komoot\n"
                            + "• Toute application de sport\n\n"
                            + "Utilisez le bouton « Exporter GPX » ou « Partager ».")
            }

            // ── Dépannage ─────────────────────────────────────────
            HelpSection {
                title: i18n.tr("Problèmes fréquents")
                body: i18n.tr("Pas de GPS ? → Sortez dehors, le ciel doit être visible\n"
                            + "Pas de bip ? → Vérifiez le volume du téléphone\n"
                            + "Carte blanche ? → Vérifiez votre connexion internet\n"
                            + "L'app plante ? → Redémarrez le téléphone\n"
                            + "Daemon bloqué ? → Ouvrez l'app, appuyez sur « Arrêter »")
            }

            // ── Lien vers À propos ───────────────────────────────
            Rectangle { width: parent.width; height: Theme.dividerHeight; color: Theme.bgCardBorder }

            Button {
                width: parent.width
                height: units.gu(6)
                text: i18n.tr("À propos d'UT WheelTrek")
                Accessible.name: text
                color: Theme.bgSection
                onClicked: pageStack.push(Qt.resolvedUrl("AboutPage.qml"))
            }
        }
    }
}

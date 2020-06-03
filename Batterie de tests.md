## Batterie de tests

|                                                                                                           | après création | après reboot  |
| --------------------------------------------------------------------------------------------------------- | -------------- | ------------- |
| dossier plein                                                                                             | dossier plein  | dossier plein |
| détection maximum files                                                                                   | OK             | OK            |
| suppression du plus ancien (ino6) du dossier                                                              | OK             | OK            |
|                                                                                                           |                |               |
| changement de politique (insmod)                                                                          | OK             | OK            |
| détection maximum files                                                                                   | OK             | OK            |
| suppression du plus gros du dossier                                                                       | OK             | OK            |
| suppression du plus gros du dossier même si ils sont tous à 0 bytes (choix arbitraire du premier fichier) | OK             | OK            |
| non suppression si i_count trop grand                                                                     | OK             | OK            |
| non suppression sur c'est un directory                                                                    | OK             | OK            |
| | changement de politique (rmmod                                                                          |                |               |
|                                                                                                           |                |               |
|                                                                                                           |                |               |
|                                                                                                           |                |               |
|                                                                                                           |                |               |
|                                                                                                           |                |               |
|                                                                                                           |                |               |

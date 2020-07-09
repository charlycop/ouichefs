#!/bin/bash

dirMount='/mnt/oui'

echo "On cre un ancien fichier de reference ? (y/n)"
read ouinon
if [ $ouinon = "y" ]; then 
        dirOld=${dirMount}'/dirold'
        mkdir ${dirOld}
        dirOld=${dirOld}'/subfolder'
        mkdir ${dirOld}
        dirOld=$dirOld'/subsubfolder'
        mkdir ${dirOld}
        fichierOld=${dirOld}'/fichierOldBig'
        touch ${fichierOld}
        echo "Je suis le fichier le plus vieux et le plus gros :)" > ${fichierOld}
        echo -e "\n\nPremier fichier (le plus vieux et plus gros) cree avec succes:\n ${fichierOld}"
fi

dirFull=${dirMount}'/dirFull'

echo -e "\n\nPret mettre 128 fichiers dans ${dirFull}? (y/n)"

read ouinon
if [ $ouinon = "y" ]; then
        mkdir ${dirFull}
        for i in `seq 1000001 1000128`;
	        do
                echo -e "\n CREATION : file$i"
                touch ${dirFull}/file$i
        done
        echo "Je suis le fichier le plus gros du dir" > ${dirFull}/file1000100
        ls ${dirFull} 
fi



echo -e "\nPret a créer file1000129->file1000150 dans ${dirFull}? (y/n)"
read ouinon
if [ $ouinon = "y" ]; then
        for i in `seq 1000129 1000150`;
                do
                echo -e "\n CREATION : file$i"
                touch ${dirFull}/file$i
        done
        ls ${dirFull}
fi
        

echo -e "\nPret a créer file1000001->file1000022 dans ${dirFull}? (y/n)"
read ouinon
if [ $ouinon = "y" ]; then
        for i in `seq 1000001 1000022`;
                        do
                        echo -e "\n CREATION : file$i"
                        touch ${dirFull}/file$i
        done
        ls ${dirFull} 
fi
        



echo -e "\n\nPret a le remplir de 128 fichiers à nouveau ? (y/n)"

read ouinon
if [ $ouinon = "y" ]; then
        for k in `seq 1000129 1000256`;
	        do
                touch ${dirFull}/file$k
        done
        ls ${dirFull}
fi

echo -e "\n\nPret a le remplir de 10000 fichiers à nouveau ? (y/n)"

read ouinon
if [ $ouinon = "y" ]; then
        for k in `seq 1 10000`;
	        do
                touch ${dirFull}/file$k
        done
        ls ${dirFull}
fi

rm ${dirFull}/*

echo -e "\n\nPret a le remplir de 303 fichiers avec 50 rotates ? (y/n)"

read ouinon
if [ $ouinon = "y" ]; then
          for i in `seq 1 50`;
          do
                 for k in `seq 1 302`;
	         do
                        touch ${dirFull}/file$k
                        echo "je rempli le fichier file${k}" > ${dirFull}/file$k
                 done
          done
          ls ${dirFull}
fi

dirfulldir=${dirMount}'/dirfulldir'

echo -e "\n\nPret a creer 128 dossier dans ${dirfulldir} ? (y/n)"

dirfulldir=${dirMount}'/dirfulldir'

read ouinon
if [ $ouinon = "y" ]; then
	mkdir $dirfulldir
	for j in `seq 1000001 1000128`;
		do
		mkdir ${dirfulldir}/dir${j}
	done
        ls $dirfulldir
fi

echo -e "\n\nPret a creer 2 dossiers supplémentaire dans ${dirfulldir} ? (y/n)"

read ouinon
if [ $ouinon = "y" ]; then
	mkdir $dirfulldir
	for j in `seq 1000129 1000130`;
		do
		mkdir ${dirfulldir}/dir${j}
	done
        ls $dirfulldir
fi

echo -e "\n\nPret supprimer le DOSSIER ${dirfulldir}/dir1000005 ? (y/n)"

read ouinon
if [ $ouinon = "y" ]; then
        rm -r ${dirfulldir}/dir1000005
        ls $dirfulldir
fi

echo -e "\n\nPret créer le FICHIER ${dirfulldir}/dir1000005 ? (y/n)"

read ouinon
if [ $ouinon = "y" ]; then
        touch ${dirfulldir}/dir1000005
        ls $dirfulldir
fi

echo -e "\n\nPret RECREER le DOSSIER ${dirfulldir}/dir1000129 ? (y/n)"

read ouinon
if [ $ouinon = "y" ]; then
        mkdir ${dirfulldir}/dir1000129
        ls $dirfulldir
fi


###############################
#PARTITION
###############################
echo -e "\n\nPret a creer 10 dossiers contenant 1 fichier chacun ? (y/n)"


read ouinon
if [ $ouinon = "y" ]; then
	for j in `seq 10001 10010`;
		do
		mkdir ${dirMount}/dir${j}
                touch ${dirMount}/dir${j}/file${j}
	done
        ls ${dirMount}
fi

dirBig=${dirMount}'/testBig'


echo -e "\n\nPret a créer ${dirBig} et le remplir de 20 fichiers de 4Mo ? (y/n)"

read ouinon
if [ $ouinon = "y" ]; then
        mkdir $dirBig

        for j in `seq 1000001 1000020`;
                do
                dd if=/dev/zero of=${dirBig}/test${j}.img bs=1M count=4 
	done
        ls ${dirBig}
fi



/*   PreMake                    
 *   Makefile Generation Utility
 *   Craig Fitzgerald           
 */                             

//#include <os2.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <io.h>
#include <ctype.h> 
#include <GnuFile.h> 
#include <GnuStr.h> 
#include <GnuArg.h> 

#define  STRSIZE    80
#define  PATHSIZE   256
#define  CMDSTRSIZE 480
#define  MACROMAX   20
#define  DATE       __DATE__ 

typedef struct
   {
   char   szName [STRSIZE];
   PSZ    pPtr;
   int    i;
   } CMDBLK;

int ProcessFile (PSZ pszInFile, FILE *fpOut);

char  szCurrFile [STRSIZE] = "none";
char  sz [512];

CMDBLK  *cbFArray = NULL;      /* array of (file list) blocks             */
CMDBLK  *cbCArray = NULL;      /* array of (cmd list) blocks              */
CMDBLK  *cbMArray = NULL;      /* array of (macro name list) blocks       */
int     iFSize = 0;            /* # elements in (file list) array         */
int     iCSize = 0;            /* # elements in (cmd list) array          */
int     iMSize = 0;            /* # elements in (macro name list) array   */

USHORT uMAXSEARCHDEPTH = 200;
BOOL   bSTRICT = FALSE;
BOOL   bNOTEST = FALSE;
BOOL   bDEBUG  = FALSE;
BOOL   bADDDOT = FALSE;


BOOL   bINCLUDELIST = FALSE;
char   szINCLUDELIST [256];

/*********************utility procs***********************/

void Usage (void)
   {
   printf ("PREMAKE makefile generation utility                       v1.2  %s \n\n", DATE);
   printf ("USAGE: PREMAKE [options] InFile [OutFile]\n\n");
   printf ("WHERE: options are 0 or more of:\n\n");
   printf ("          /Depth=# ... # lines to read searching for includes (def=200)\n");
   printf ("          /Strict .... Die if file is not found.\n");
   printf ("          /NoTest .... Dont look for Include file existance.\n");
   printf ("          /Debug ..... Dubug info.\n");
   printf ("          /AddDot .... Add dot to ext. (for compatibility).\n");
   printf ("          \n");
   printf ("PREMAKE commands recognized in input file:\n\n");
   printf ("   :FILELIST   ListName     = { filename1,filename2, .... ,filenameN }\n");
   printf ("   :CMDLIST    CmdListName  = { commandline1, ... ,commandlineN } \n");
   printf ("   :EXPAND     ListName,CmdListName,Path1,Ext1,Path2,Ext2[,INC|NOI|NOD|PLI]\n");
   printf ("   :MAKELIST   ListName,MacroBaseName,Path,Ext\n");
   printf ("   :INCLUDE    FileName\n\n");
   printf ("   :INCLUDELIST path;path;path...\n\n");
   printf ("   {C: CmdListName} .............. Expand to commands\n");
   printf ("   {I: Filename} ................. Expand to #include names\n");
   printf ("   {P: Filename} ................. Expand to %%include names\n");
   printf ("   {F: ListName,Path,Ext} .... Expand to file names\n");
   printf ("   {L: ListName,Path,Ext[,ct]} Expand to file names (breaks lines & adds +)\n");
   printf ("   {X: ListName,Path,Ext,delim,prefix,ct} Expand to file names (breaks lines...)\n");
   printf ("   {M: MacroBaseName} ............ Expand to $(macro names)\n");
   exit (0);
   }


void Error (PSZ psz1, PSZ psz2)
   {
   fprintf (stderr, "PreMake Error (%s:%ld): ", szCurrFile, FilGetLine ());
   fprintf (stderr, psz1, psz2);
   exit (1);
   }


void Warning (PSZ psz1, PSZ psz2)
   {
   fprintf (stderr, "PreMake Warning (%s:%ld): ", szCurrFile, FilGetLine ());
   fprintf (stderr, psz1, psz2);
   fprintf (stderr, "\n");
   if (bSTRICT)
      exit (1);
   }




/* returns first nonblank and leaves it in stream */
int SkipSpace (FILE *fp, BOOL bEOLOK)
   {
   FilReadWhile (fp, NULL, (bEOLOK ? " \t\n" : " \t"), 0, FALSE);
   return FilPeek (fp);
   }


/* returns 0 if EOF */
int SkipLine (FILE *fp)
   {
   FilReadTo (fp, NULL, "\n", 0, TRUE);
   return FilPeek (fp) != EOF;
   }


void GobbleChar (FILE *fp, char cExpected)
   {
   int   c;

   SkipSpace (fp, TRUE);
   if ((c = f_getc (fp)) == cExpected)
      return;

   printf ("PreMake Error(%s:%ld): Illegal Character, %c when %c expected",
                           szCurrFile, FilGetLine (), c, cExpected);
   exit (0);
   }


/* reads printable only 0 if error*/
int RReadStr (FILE *fpIn, PSZ pszStr)
   {
   int  c;

   *pszStr = '\0';

   if (!isgraph (SkipSpace (fpIn, FALSE)))
      return 0;
   while (isgraph (c = f_getc (fpIn)) && c != ',' && c != '}')
      *pszStr++ = (char) c;
   f_ungetc (c, fpIn);
   *pszStr = '\0';
   return 1;
   }




/* returns 0 if an error or not a " */
int ReadQuoteStr (FILE *fp, PSZ pszWord)
   {
   int   c;

   *pszWord = '\0';
   GobbleChar (fp,'"');
   while ((c = f_getc (fp)) != '"' && c != '\n' && c != EOF)
     *pszWord++ = (char) c;
   *pszWord = '\0';
   return c = '"'; 
   }



int ReadCSV (FILE *fpIn, PSZ pszStr, BOOL bMode)
   {
   int  c;

   if ((c = SkipSpace (fpIn, bMode)) == '"')
      {
      if (!ReadQuoteStr (fpIn, pszStr))
         Error ("No closing quote found", NULL);
      }
   else 
      if (!RReadStr (fpIn, pszStr))
         return 0;
   SkipSpace (fpIn, bMode);
   if ((c = f_getc (fpIn)) == ',')
      return 1;
   else if (c == EOF || (bMode && c == '}') ||
           (!bMode && c == '\n'))
      return 0;
   Error ("Delimeter or Terminator expected",NULL);
   return 0;
   }


void MakeFileName (PSZ pszOut, PSZ pszPrefix, PSZ pszMain, PSZ pszExt)
   {
   if (bADDDOT && pszExt && *pszExt)
      sprintf (pszOut, "%s%s.%s ", pszPrefix, pszMain, pszExt);
   else
      sprintf (pszOut, "%s%s%s ", pszPrefix, pszMain, pszExt);
   }



void PrintFileName (FILE *fpOut, PSZ pszPrefix, PSZ pszMain, PSZ pszExt)
   {
   char szOut [PATHSIZE];

   MakeFileName (szOut, pszPrefix, pszMain, pszExt);
   fputs (szOut, fpOut);
   }



int FindIndex (CMDBLK cbBlock[], PSZ pszStr, int BlockSize)
   {
   int i;

   for (i = 0; i < BlockSize; i++)
      if (strcmp (pszStr, cbBlock[i].szName) == 0)
         return i;
   return -1;
   }



/* also initializes elements in list */
/* die on allocation error */
CMDBLK *ResizeBlockList (CMDBLK cbBlock[], int iSize)
   {
   if ((cbBlock = realloc (cbBlock, iSize * sizeof (CMDBLK))) == NULL)
      Error ("Error ReAllocating CMDBLK",NULL);
   cbBlock[iSize - 1].szName[0] = '\0';
   cbBlock[iSize - 1].pPtr = NULL;
   cbBlock[iSize - 1].i = 0;
   return cbBlock;
   }


/*************************************************/

int ProcessAssignmentList (FILE *fpIn, CMDBLK cbBlock [], int iBIndex, int iESize)
   {
   char szTmpStr [CMDSTRSIZE];
   int  cTerm, iSize;

   do
      {
      if ((cTerm =ReadCSV (fpIn, szTmpStr, TRUE)) == EOF)
         Error ("EOF in processing list value",NULL);
      if (szTmpStr[0] == '\0')
         break;
      iSize = cbBlock [iBIndex].i += 1;
      cbBlock [iBIndex].pPtr = realloc (cbBlock [iBIndex].pPtr,iSize * iESize);
      strcpy (cbBlock[iBIndex].pPtr + (iSize -1) * iESize, szTmpStr); 
      }
   while (cTerm);
   return 1;   /* for later */
   }



CMDBLK *ReadBlkName (FILE *fpIn, CMDBLK cbBlkArray[], int *iBlkSize, int *iIndex)
   {
   int iTest;
   char szNameStr[STRSIZE];

   if (SkipSpace (fpIn, FALSE) != '{')
      {
      iTest =ReadCSV (fpIn, szNameStr, TRUE);
      if ((*iIndex = FindIndex (cbBlkArray, szNameStr, *iBlkSize)) == -1)
         Error ("Undefined FileListName or CmdListName: %s",szNameStr);
      return cbBlkArray;
      }
   GobbleChar (fpIn, '{');
   *iIndex = *iBlkSize;
   *iBlkSize += 1;
   cbBlkArray = ResizeBlockList (cbBlkArray, *iBlkSize);
   strcpy (cbBlkArray[*iIndex].szName, "&temp&");
   ProcessAssignmentList (fpIn, cbBlkArray, *iIndex, STRSIZE);
   SkipSpace (fpIn, FALSE);
   GobbleChar (fpIn, ',');
   return cbBlkArray;
   }


/* returns 0 on error */
int ProcessFileList (FILE *fpIn)
   {
   int   iResult;

   cbFArray = ResizeBlockList (cbFArray, ++iFSize);
   if (!RReadStr (fpIn, cbFArray[iFSize-1].szName))
      Error ("Illegal char or EOF in processing file list ID",NULL);
   GobbleChar (fpIn, '=');
   GobbleChar (fpIn, '{');
   iResult = ProcessAssignmentList (fpIn, cbFArray, iFSize - 1, STRSIZE);
   SkipLine (fpIn);
   return iResult;
   }


/* returns 0 on error */
int ProcessCmdList (FILE *fpIn)
   {
   int   iResult;

   cbCArray = ResizeBlockList (cbCArray, ++iCSize);
   if (!RReadStr (fpIn, cbCArray[iCSize-1].szName))
      Error ("Illegal char or EOF in reading Cmd list ID",NULL);
   GobbleChar (fpIn, '=');
   GobbleChar (fpIn, '{');
   iResult = ProcessAssignmentList (fpIn, cbCArray, iCSize - 1, CMDSTRSIZE);
   SkipLine (fpIn);
   return iResult;
   }


/* returns 0 on error */
int ProcessIncludeList (FILE *fpIn)
   {
   FilReadWord (fpIn, szINCLUDELIST, " \t", " \t\n", 256, TRUE);
   bINCLUDELIST = TRUE;
   return 1;
   }




/* this process defines  a/some make macro(s) out of a file list*/
int ProcessMakeList (FILE *fpIn, FILE *fpOut)
   {
   char  szExtStr [STRSIZE]; 
   char  szPathStr [STRSIZE];
   int   i, iIndex, iLocalCount, iFileCount;

   cbMArray = ResizeBlockList (cbMArray, ++iMSize);
   cbFArray = ReadBlkName (fpIn, cbFArray, &iFSize, &iIndex);
   if (!ReadCSV (fpIn, cbMArray[iMSize-1].szName, FALSE) ||
       !ReadCSV (fpIn, szPathStr, FALSE))
      Error ("Not enough parameters for MakeList",NULL);
   if (ReadCSV (fpIn, szExtStr, FALSE))
      Error ("Too many parameters for MakeList",NULL);
   cbMArray [iMSize -1].i = (cbFArray [iIndex].i -1) / MACROMAX + 1;
   iFileCount = 1;
   for (i = 0; i < cbMArray [iMSize -1].i; i++)
      {
      fprintf (fpOut, "\n%s%d=", cbMArray[iMSize-1].szName, i);
      iLocalCount = 0;
      while (iFileCount <= cbFArray [iIndex].i && iLocalCount < MACROMAX)
         {
         PrintFileName (fpOut, szPathStr, 
                     cbFArray [iIndex].pPtr + STRSIZE * (iFileCount - 1),
                     szExtStr);
         if (!(iFileCount % 4) && iFileCount != cbFArray [iIndex].i)
            fprintf (fpOut, "\\\n      ");
         iFileCount++;
         iLocalCount++;
         }
      }
   SkipLine (fpIn);
   return 1;
   }


/* returns 0 if no more paths */
int GetPath (char *pszEnv, int iPathNum, char *pszPath)
   {
   char *psz;
   int  i;

   pszPath[0] = '\0';

   if (!iPathNum)
      return 1;

   if (bINCLUDELIST)
      psz = szINCLUDELIST;
   else
      psz = getenv (pszEnv);

   for (i=0; i<iPathNum-1 && *psz != '\0'; i += *(psz++) == ';')
      ;
   if (*psz == '\0')
      return 0;

   strcpy (pszPath, psz);
   if ((psz = strchr (pszPath, ';')) != NULL)
      *psz = '\0';
   strcat (pszPath, "\\");
   return 1;
   }





int Exists (char *pszPath, char *pszFile)
   {
   char  sztmp [STRSIZE];

   strcat (strcpy (sztmp, pszPath), pszFile);
   return !access (sztmp, 00);
   }




/* returns 0 on file not found */
int PrintIncludes (FILE *fpOut, PSZ pszMain, PSZ pszPath, PSZ pszExt)
   {
   char  szTmpName   [STRSIZE];
   char  szIncPath   [PATHSIZE];
   char  szIncFile   [STRSIZE];
   char  szParentFile[STRSIZE];
   FILE  *fpTmp;
   USHORT i, j;
   BOOL  bExists;
   PSZ   psz;
   ULONG ulOldLineNo;

   
   MakeFileName (szTmpName, pszPath, pszMain, pszExt);

   if ((fpTmp = fopen (szTmpName, "rt")) == NULL)
      {
      Warning ("File not found- (%s)", szTmpName);
      return 0;
      }

   ulOldLineNo = FilGetLine ();
   FilSetLine (1);
   strcpy (szParentFile, szCurrFile);
   strcpy (szCurrFile, szTmpName);

   /*--- look for #include lines in file ---*/
   for (i=0; i<uMAXSEARCHDEPTH; i++)
      {
      if (FilReadLine (fpTmp, sz, "", sizeof sz) == -1)
         break;

      if (*sz != '#' || strncmp (sz+1, "include", 7))
         continue;

      psz = sz + 8;
      if (StrGetWord (&psz, szIncFile, " \t\n", " \t;\n", FALSE, TRUE) == -1)
         continue;

      if (*StrStrip (szIncFile, " \t") != '"')
         continue;

      StrClip (StrStrip (szIncFile, "\""), " \t\"");

      /*--- see if #include file actually exists ---*/
      *szIncPath = '\0';
      if (!bNOTEST)
         for (bExists= j= 0; GetPath ("INCLUDE", j, szIncPath); j++)
            if (bExists = Exists (szIncPath, szIncFile))
               break;

      PrintFileName (fpOut, (bExists ? szIncPath : ""), szIncFile, "");

      if (!bExists && !bNOTEST)
         Warning ("Include File not found- (%s)", szIncFile);
      }
   fclose (fpTmp);
   FilSetLine (ulOldLineNo);
   strcpy (szCurrFile, szParentFile);

   return 1;
   }



/* returns 0 on file not found */
int PrintPLIIncludes (FILE *fpOut, PSZ pszMain, PSZ pszPath, PSZ pszExt)
   {
   char  szTmpName   [STRSIZE];
   char  szIncPath   [PATHSIZE];
   char  szIncFile   [STRSIZE];
   char  szParentFile[STRSIZE];
   FILE  *fpTmp;
   USHORT i, j;
   BOOL  bExists;
   PSZ   psz;
   ULONG ulOldLineNo;

   
   MakeFileName (szTmpName, pszPath, pszMain, pszExt);

   /*--- fix $$ in file names ---*/
   for (i=0; szTmpName[i]; i++)
      {
      if (szTmpName[i] == '$' && szTmpName[i+1] == '$')
         {
         for (j=i; szTmpName[j]; j++)
            szTmpName[j] = szTmpName[j+1];
         }
      }

   if ((fpTmp = fopen (szTmpName, "rt")) == NULL)
      {
      Warning ("File not found- (%s)", szTmpName);
      return 0;
      }

   ulOldLineNo = FilGetLine ();
   FilSetLine (1);
   strcpy (szParentFile, szCurrFile);
   strcpy (szCurrFile, szTmpName);

   /*--- look for #include lines in file ---*/
   for (i=0; i<uMAXSEARCHDEPTH; i++)
      {
      if (FilReadLine (fpTmp, sz, "", sizeof sz) == -1)
         break;

      StrStrip (sz, " \t");

      if (*sz != '%' || strnicmp (sz+1, "include", 7))
         continue;

      psz = sz + 8;

      if (StrGetWord (&psz, szIncFile, " \t\n", "; \t", FALSE, TRUE) == -1)
         continue;

      StrClip (StrStrip (szIncFile, " \t"), " \t;");

      strcat (szIncFile, ".inc");

      /*--- see if #include file actually exists ---*/
      *szIncPath = '\0';
      if (!bNOTEST)
         for (bExists= j= 0; GetPath ("INCLUDE", j, szIncPath); j++)
            if (bExists = Exists (szIncPath, szIncFile))
               break;

      PrintFileName (fpOut, (bExists ? szIncPath : ""), szIncFile, "");

      if (!bExists && !bNOTEST)
         Warning ("Include File not found- (%s)", szIncFile);
      }
   fclose (fpTmp);
   FilSetLine (ulOldLineNo);
   strcpy (szCurrFile, szParentFile);

   return 1;
   }



/* the expand of the format  text {macrolist/filelist/cmdlist/filename} text 
 *
 * leading { already read
 *
 */
int ExpandCmd (FILE *fpIn, FILE *fpOut)
   {
   char  szPathStr [STRSIZE];
   char  szMainStr [STRSIZE];
   char  szExtStr  [STRSIZE];
   char  szCtStr   [STRSIZE];
   char  szDelimStr [STRSIZE];
   char  szPrefixStr[STRSIZE];
   int   c, i, j, iIndex;

   c = f_getc (fpIn);
   switch (toupper(c))
      {
      case 'C': /* expand cmd string list */
         GobbleChar (fpIn, ':');
         ReadCSV (fpIn, szMainStr, TRUE);
         if ((iIndex = FindIndex (cbCArray, szMainStr, iCSize)) < 0)
            Error ("Undefined CommandName in Expand %s",szMainStr);
         for (i = 0; i < cbCArray [iIndex].i; i++)
            fprintf (fpOut, "   %s\n", cbCArray[iIndex].pPtr+CMDSTRSIZE*i);
         break;

      case 'I': /* create includes */
         GobbleChar (fpIn, ':');
         ReadCSV (fpIn, szMainStr, TRUE);
         fprintf (fpOut,"%s ", szMainStr );
         if (!PrintIncludes (fpOut, szMainStr, "", ""))
            Error ("File Not Found %s",szMainStr);
         break;

      case 'P': /* create pli includes */
         GobbleChar (fpIn, ':');
         ReadCSV (fpIn, szMainStr, TRUE);
         fprintf (fpOut,"%s ", szMainStr );
         if (!PrintPLIIncludes (fpOut, szMainStr, "", ""))
            Error ("File Not Found %s",szMainStr);
         break;

      case 'F': /* expand file list */
         GobbleChar (fpIn, ':');
         cbFArray = ReadBlkName (fpIn, cbFArray, &iFSize, &iIndex);
         if (!ReadCSV (fpIn, szPathStr, TRUE))
            Error ("More parameters expected %s","'{F:name,path,ext}'");
         ReadCSV (fpIn, szExtStr, TRUE);
         for (i = 0; i < cbFArray [iIndex].i; i++)
            PrintFileName (fpOut, szPathStr,
                            cbFArray [iIndex].pPtr + STRSIZE * i, szExtStr);
         break;

      case 'L': /* expand file list #2 */
         j = 5;
         GobbleChar (fpIn, ':');
         cbFArray = ReadBlkName (fpIn, cbFArray, &iFSize, &iIndex);
         if (!ReadCSV (fpIn, szPathStr, TRUE))
            Error ("More parameters expected %s","'{L:name,path,ext[,ct]}'");
         if (ReadCSV (fpIn, szExtStr, TRUE))
            {
            ReadCSV (fpIn, szCtStr, TRUE);
            j = atoi (szCtStr);
            }
         for (i = 0; i < cbFArray [iIndex].i; i++)
            {
            PrintFileName (fpOut, szPathStr,
                            cbFArray [iIndex].pPtr + STRSIZE * i, szExtStr);
            if (!((i+1) % j) && i+1 < cbFArray [iIndex].i)
               fprintf (fpOut, "+\n");
            }
         break;

      case 'V': /* expand file list #n */
         j = 5;
         GobbleChar (fpIn, ':');
         cbFArray = ReadBlkName (fpIn, cbFArray, &iFSize, &iIndex);
         if (!ReadCSV (fpIn, szPathStr, TRUE))
            Error ("More parameters expected %s","'{L:name,path,ext[,ct]}'");
         if (ReadCSV (fpIn, szExtStr, TRUE))
            {
            ReadCSV (fpIn, szCtStr, TRUE);
            j = atoi (szCtStr);
            }
         for (i = 0; i < cbFArray [iIndex].i; i++)
            {
            PrintFileName (fpOut, szPathStr,
                            cbFArray [iIndex].pPtr + STRSIZE * i, szExtStr);
            if (!((i+1) % j) && i+1 < cbFArray [iIndex].i)
               fprintf (fpOut, "\n");
            }
         break;

      case 'X': /* expand file list #3 */
         GobbleChar (fpIn, ':');
         cbFArray = ReadBlkName (fpIn, cbFArray, &iFSize, &iIndex);
         if (!ReadCSV (fpIn, szPathStr, TRUE))
            Error ("More parameters expected %s","'{X:blkname,path,ext,delim,lineprefixstring,count}'");
         if (!ReadCSV (fpIn, szExtStr, TRUE))
            Error ("More parameters expected %s","'{X:blkname,path,ext,delim,lineprefixstring,count}'");
         if (!ReadCSV (fpIn, szDelimStr, TRUE))
            Error ("More parameters expected %s","'{X:blkname,path,ext,delim,lineprefixstring,count}'");
         if (!ReadCSV (fpIn, szPrefixStr, TRUE))
            Error ("More parameters expected %s","'{X:blkname,path,ext,delim,lineprefixstring,count}'");
         ReadCSV (fpIn, szCtStr, TRUE);
         j = atoi (szCtStr);

         if (!stricmp (szDelimStr, "comma"))
            strcpy (szDelimStr, ",");

         for (i=0; i<cbFArray [iIndex].i; i++)
            {
            if (!(i % j))  // SOL only
               fprintf (fpOut, "%s ", szPrefixStr);

            PrintFileName (fpOut, szPathStr,
                            cbFArray [iIndex].pPtr + STRSIZE * i, szExtStr);

            if (!((i+1) % j) && i+1 < cbFArray [iIndex].i) // EOL only
               fprintf (fpOut, "\n");
            else if (((i+1) % j) && i+1 < cbFArray [iIndex].i)
               fprintf (fpOut, "%s", szDelimStr);  // not eol
            }
         break;

      case 'M': /* expand macro names */
         GobbleChar (fpIn, ':');
         ReadCSV (fpIn, szMainStr, TRUE);
         if ((iIndex = FindIndex (cbMArray, szMainStr, iMSize)) < 0)
            Error ("Undefined MacroName %s",szMainStr);
         for (i = 0; i < cbMArray [iIndex].i; i++)
            fprintf (fpOut, "$(%s%d) ", cbMArray [iIndex].szName, i);
         break;

      default:
         putc ('{', fpOut);
         putc (c, fpOut);
      }
   return c;
   }






/* the expand of the format:  Expand:*/
int ProcessExpand (FILE *fpIn, FILE *fpOut)
   {
   char  szPath1Str[STRSIZE], szPath2Str[STRSIZE];
   char  szExt1Str[STRSIZE],  szExt2Str[STRSIZE];
   char  szTmpStr[STRSIZE],   szExtraStr[STRSIZE];
   char  *pszTmpPtr;
   int   i, j, iCIndex, iFIndex;
   BOOL  bIncludes    = FALSE, 
         bPLIIncludes = FALSE, 
         bExtra       = FALSE, 
         bDependency  = FALSE;

   cbFArray = ReadBlkName (fpIn, cbFArray, &iFSize, &iFIndex);
   cbCArray = ReadBlkName (fpIn, cbCArray, &iCSize, &iCIndex); 
   if (!ReadCSV (fpIn, szPath1Str, FALSE) ||
       !ReadCSV (fpIn, szExt1Str, FALSE) ||
       !ReadCSV (fpIn, szPath2Str, FALSE) )
      Error ("Not Enough Parameters for :Expand command",NULL);

   if (bDEBUG)
      fprintf (stderr, "Processing Expand F:%s Fcount:%d C:%s \n",
         cbFArray[iFIndex].szName, cbFArray[iFIndex].i,
         cbCArray[iFIndex].szName);

/*
 * Changed to allow additional information in the expand command
 *
 */

   bExtra      = FALSE;
   bDependency = TRUE;
   if (ReadCSV(fpIn, szExt2Str, FALSE))
      {
      /*--- read Includes / No Includes Param ---*/
      if (ReadCSV(fpIn, szTmpStr, FALSE))
         {
         /*--- Read Extra Stuff ---*/
         if (ReadCSV(fpIn, szExtraStr, FALSE))
            Error ("Too many parameters for :Expand command", NULL);
         bExtra = TRUE;
         }
      if (strnicmp (szTmpStr, "inc", 3) == 0)
         bIncludes = TRUE;
      else if (strnicmp (szTmpStr, "pli", 3) == 0)
         bPLIIncludes = TRUE;
      else if (strnicmp (szTmpStr, "noi", 3) == 0)
         bIncludes = FALSE;
      else if (strnicmp (szTmpStr, "nod", 3) == 0)
         {
         bIncludes = FALSE;
         bDependency = FALSE;
         }
      else
         Error ("Unrecognized parameter in :Expand cmd: %s",szTmpStr);
      }

   for (i = 0; i < cbFArray [iFIndex].i; i++)
      {
      pszTmpPtr = cbFArray [iFIndex].pPtr + STRSIZE * i;

      PrintFileName (fpOut, szPath1Str ,pszTmpPtr, szExt1Str);
      fprintf (fpOut, ": ");
      
      if (bDependency)
         PrintFileName (fpOut, szPath2Str ,pszTmpPtr, szExt2Str);
      if (bIncludes)
         PrintIncludes (fpOut, pszTmpPtr, szPath2Str, szExt2Str);
      if (bPLIIncludes)
         PrintPLIIncludes (fpOut, pszTmpPtr, szPath2Str, szExt2Str);
      if (bExtra)
         fprintf (fpOut, " %s", szExtraStr);
      for (j = 0; j < cbCArray[iCIndex].i; j++)
         fprintf (fpOut, "\n   %s", cbCArray [iCIndex].pPtr + CMDSTRSIZE * j);
      fprintf (fpOut, "\n\n");
      }
   SkipLine (fpIn);
   return 1;
   }


int ProcessInclude (FILE *fpIn, FILE *fpOut)
   {
   char  szName[STRSIZE];

   if (!RReadStr (fpIn, szName))
      Error ("Illegal char or EOF in processing :INCLUDE", NULL);

   if (bDEBUG)
      fprintf (stderr, "Processing Include %s\n", szName);

   return ProcessFile (szName, fpOut);
   }


int ProcessCmd (FILE *fpIn, FILE *fpOut)
   {
   char  szWord [STRSIZE];

   if (!RReadStr (fpIn, szWord))
      Error ("Illegal char or EOF while reading CMD: %s",szWord);
   if (stricmp (szWord,"filelist") ==0)
      return ProcessFileList (fpIn);
   if (stricmp (szWord,"cmdlist") ==0)
      return ProcessCmdList (fpIn);
   if (stricmp (szWord,"makelist") ==0)
      return ProcessMakeList (fpIn, fpOut);
   if (stricmp (szWord,"expand") ==0)
      return ProcessExpand (fpIn, fpOut);
   if (stricmp (szWord,"include") ==0)
      return ProcessInclude (fpIn, fpOut);
   if (stricmp (szWord,"includelist") ==0)
      return ProcessIncludeList (fpIn);
   Error ("Illegal Command Name: %s", szWord);
   }


/* returns 0 on EOF */
int ProcessLine (FILE *fpIn, FILE *fpOut)
   {
   BOOL bFirstChar = TRUE;
   int ch;
   
   do
      {
      ch = f_getc (fpIn);
      if (ch ==':' && bFirstChar)
         return ProcessCmd (fpIn, fpOut);
      if (ch =='~' && bFirstChar)
         return SkipLine (fpIn);
      if (ch != ' ')
         bFirstChar = FALSE;
      if (ch == '{')
         ExpandCmd (fpIn, fpOut);
      else if (ch == EOF)
         return 0;
      else
         putc (ch, fpOut);
      }
   while (ch != '\n' && ch != EOF);
   return ch != EOF;
   }



int ProcessFile (PSZ pszInFile, FILE *fpOut)
   {
   FILE *fpIn;
   ULONG ulOldLineNo;

   if (bDEBUG)
      fprintf (stderr, "Processing File %s\n", pszInFile);

   if ((fpIn  = fopen (pszInFile, "r")) == NULL)
      Error ("Cannot open input file: %s",pszInFile);

   strcpy (szCurrFile, pszInFile);
   ulOldLineNo = FilGetLine ();

   FilSetLine (1);
   while (ProcessLine (fpIn, fpOut))
      ;

   FilSetLine (ulOldLineNo);
   fclose (fpIn);
   return 1;
   }


//
// uMAXSEARCHDEPTH = max lines to search for #include
// bSTRICT         = die if file or #include not found
//
//
//
//
//
//
//
//
int _cdecl main (int argc, PSZ argv[])
   {
   FILE  *fpOut;
   char  szInFile [STRSIZE];
   char  szOutFile [STRSIZE];
   char  *psz;


   ArgBuildBlk ("*^debug ? *^help *^Depth% *^Strict *^NoTest *^AddDot");

   if (ArgFillBlk (argv))
      {
      fprintf (stderr, "%s\n", ArgGetErr ());
      Usage ();
      }

   if (ArgIs ("?") || ArgIs ("help") || !ArgIs (NULL))
      Usage ();

   if (ArgIs ("Depth"))
      uMAXSEARCHDEPTH = atoi (ArgGet ("Depth", 0));

   bSTRICT = ArgIs ("Strict");
   bNOTEST = ArgIs ("NoTest");
   bDEBUG  = ArgIs ("debug");
   bADDDOT = ArgIs ("AddDot");

   strcpy (szInFile, ArgGet(NULL, 0));

   if (ArgIs (NULL) > 1)
      strcpy (szOutFile, ArgGet(NULL, 1));
   else
      {
      strcpy (szOutFile, szInFile);
      if (((psz = strchr (szOutFile, '.')) == NULL) || stricmp (psz, ".pre"))
         Error ("Second param required unless 1st param has .pre extension",NULL);
      *psz = '\0';
      printf ("Premake Message: using %s for output filename\n", szOutFile);
      }

   if ((fpOut = fopen (szOutFile, "w")) == NULL)
      Error ("Cannot open output file: %s",szOutFile);
   ProcessFile (szInFile, fpOut);
   fclose (fpOut);
   printf ("\nDone.\n");
   return 0;
   }

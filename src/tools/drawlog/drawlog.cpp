/*******************************************************************
*
*  DESCRIPTION: Draws the content of the log 
*
*  AUTHOR:    Amir Barylko & Jorge Beyoglonian 
*  Version 2: Daniel Rodriguez
*  Version 3: Gabriel Wainer
*  Version 4: Alejandro Troccoli (Parallel )
*
*  EMAIL: mailto://amir@dc.uba.ar
*         mailto://jbeyoglo@dc.uba.ar
*         mailto://drodrigu@dc.uba.ar
*         mailto://gabrielw@dc.uba.ar
*
*  DATE: 27/06/1998
*  DATE: 28/04/1999 (v2)
*
*******************************************************************/

// ** include files **//
#include <cstdlib>
#include <strstream>
#include <fstream>
#include <cstring>
#include "ini.h"     // Class Ini
#include "VTime.h"    // Class VTime
#include "logparser.h"
#include "cellstate.h"

#include "cnpy.h"

using namespace std;

static const string defaultPortName("out"); /* AtomicCell::outPort */

// ** main ** //

VTime getNextMsgLine( istream& file, const string& modelName, char* buffer, const string &portName );

void printState( const CellState &state, const VTime &time )
{

	if (!Impresion::Default.FlatLog()) 
		cout << "Time: " << time.asString() << endl ;
	else
		cout << endl;
	state.print(cout, '?');
}

void showHelp()
{
	cout << "drawlog -[?hmtclwp0n]\n\n";
	cout << "where:\n";
	cout << "\t?\tShow this message\n";
	cout << "\th\tShow this message\n";
	cout << "\tm\tSpecify file containing the model (.ma)\n";
	cout << "\tt\tInitial time\n";
	cout << "\ti\tTime interval (After the initial time, draw after every time interval)\n";
	cout << "\tc\tSpecify the coupled model to draw\n";
	cout << "\tl\tLog file containing the output generated by SIMU\n";
	cout << "\tw\tWidth (in characters) used to represent numeric values\n";
	cout << "\tp\tPrecision used to represent numeric values (in characters)\n";
	cout << "\t0\tDon't print the zero value\n";
	cout << "\tf\tOnly cell values on a specified slice in 3D models\n";
	cout << "\tn\tSpecify the neighbor port to show (default: " << defaultPortName << ")\n";
	exit(1);
}


int splitLine( char *line, char *pos, char *value)
{
	char    *posi = pos, *val = value;

        // Primero leo POS
	while ( *line != 0 && *line != '=' )
	{
		if (*line != ' '){
			*posi = *line;
			posi++;
		}
		line++;
	}
	if (*line == 0)
		return 0;

	*posi = 0;
	line++;

        // Ahora leo el value
	while ( *line != 0 )
	{
		if (*line != ' '){
			*val = *line;
			val++;
		}
		line++;
	}
	*val = 0;

	if (pos[0] == '(' && strlen(value) > 0)
		return 1;

	return 0;
}


//////////////////////////////////////////////////////////////////////////////
// MAIN - DRAWLOG
//////////////////////////////////////////////////////////////////////////////
int main( int argc, char *argv[] )
{
	try
	{
		VTime initial( VTime::Zero );
		VTime timeInterval(VTime::InvalidTime);
		string modelName, iniName, logName("-"), strWidth(""), strPrec(""), strPlane("");
		string strPort("");

		// parameter parsing

		while( --argc )
			if( *argv[ argc ] == '-' )
				switch( argv[ argc ][ 1 ] )
			{
				case 'm': /* file .ma */
					iniName = argv[ argc ] + 2;
					break;

				case 't': /* intital time */ 
					initial = argv[ argc ] + 2 ;
					break;

				case 'i': /* time interval */
					timeInterval = argv[ argc ] + 2;
					break;

				case 'c': /* coupled */
					modelName = lowerCase(argv[ argc ] + 2);
					break;

				case 'l': /* log filename */
					logName = argv[ argc ] + 2 ;
					break;

				case 'w': /* Set width */
					strWidth = argv [ argc ] + 2;
					Impresion::Default.Width( str2Int(strWidth) );
					break;

				case 'p': /* Set precision */
					strPrec = argv [ argc ] + 2;
					Impresion::Default.Precision ( str2Int(strPrec) );
					break;

				case '0': /* Don't print zero */
					Impresion::Default.PrintZero(false);
					break;

				case '?':
				case 'h':
					showHelp();
					break;

				case 'f': /* Only cell values on 3D models */
					Impresion::Default.FlatLog(true);
					strPlane = argv [ argc ] + 2;
					Impresion::Default.FlatLogPlane( str2Int(strPlane) );
					break;
					
				case 'n': /* The port to show */
					strPort = argv [ argc ] + 2;
					break;

				default:
					cout << "Warning... invalid parameter " << argv[ argc ] << "!" << endl ;
					showHelp();
			}
			else
				cout << "Warning... invalid parameter " << argv[ argc ] << "!"	<< endl ;

		// parameter validation

		if( iniName == "" || modelName == "" )
		{
			cout << "Drawlog - Parallel Version" << endl;
			cout << "Usage: " << argv[ 0 ] << " -mfile.ma -cCoupledCellName [ -tInitialtime ]\n"
			        "\t-lmessage.log  [ -wWidth ] [ -pPrecision ] [ -0 ]\n"
				"\t-nNCPort" << endl;

			return 1 ;
		}

		Ini iniFile ;
		Ini modelsLogFiles;

		iniFile.parse( iniName ) ;

		//If reading from files
		if ( logName != "-")
			modelsLogFiles.parse( logName );

		// dimension
		nTupla			nt;
		register unsigned	cols = 0, rows = 0;
		if (iniFile.exists( modelName, "width" ))
		{
			cols = str2Int( iniFile.definition( modelName, "width"  ).front() );
			rows = str2Int( iniFile.definition( modelName, "height" ).front() );

			nt.add(rows, cols);
		}
		else if (iniFile.exists( modelName, "dim" ))
		{
			Ini::IdList	dimension = iniFile.definition( modelName, "dim" );
			CellPosition	cp( iniFile.join(dimension) );

			nt = cp;

			cols = nt.get(DIM_WIDTH);
			rows = nt.get(DIM_HEIGHT);
		}

		if ( nt.contains(0) )
			MASSERT_ERR( "Attemp to draw a model where a component of its dimension is 0" );

		//////////////////////////////////////////////////////////////
		// validate port name
		//////////////////////////////////////////////////////////////
		if (strPort.empty())
			strPort = defaultPortName;
		else if (strPort != defaultPortName) {
			MASSERTMSG( iniFile.exists( modelName, "neighborports" ),
			             "Port requested (-n) but no NeighborPort keyword in the model" );
			
			Ini::IdList::const_iterator cursor;
			const Ini::IdList &nc_ports( iniFile.definition( modelName, "neighborports" ) ) ;

			cursor = nc_ports.begin();
			while (cursor != nc_ports.end() && *cursor != strPort)
				cursor++;
			
			MASSERTMSG( cursor != nc_ports.end(), "Port '" + strPort +"' not declared" );
			
			strPort = "out_" + strPort; /* Add the prefix (AtomicCell::NCOutPrefix) */
		}
		//////////////////////////////////////////////////////////////

		CellState state( nt ) ;

		//////////////////////////////////////////////////////////////
		// default initial value
		string initialValue( iniFile.definition( modelName, "initialvalue" ).front() ) ;

		Real value ( str2Real( initialValue ) );

		CellPosition	counter( nt.dimension(), 0);
		register bool	overflow = false;

		while (!overflow)
		{
			state[ counter ] = value;
			overflow = counter.next( nt );
		}
		/////////////////////////////////////////////////////////////

		/////////////////////////////////////////////////////////////
		// loading the initial state
		/////////////////////////////////////////////////////////////
		if( iniFile.exists( modelName, "initialrow" ) )		// The dim = 2
		{
			const Ini::IdList &values( iniFile.definition( modelName, "initialrow" ) ) ;
			register unsigned row = 0;
			Ini::IdList::const_iterator cursor = values.begin();

			while (cursor != values.end() )
			{
				// el primer valor es la fila
				row = str2Int( (*cursor) ) ;
				MASSERTMSG( row <= rows-1, "The number of row for initialRowValue is out of range. It's " + int2Str(row) + " and must be in [ 0, " + int2Str( rows - 1 ) + "]" );

				cursor++ ;

				// Los siguientes elementos son la descripcion de la fila
				register unsigned col = 0;

				while ( col < cols )
				{
					MASSERTMSG( cursor != values.end(), "Insuficient data for initialRowValue. Last row with " + int2Str(col) + " elements. Note: May be a middle row definition with less elements.");
					string rowVal( (*cursor) ) ;

					nTupla	nt3;
					nt3.add(row, col);
					state[ nt3 ] = str2Real( rowVal ) ;
					col++ ;
					cursor++ ;
				}
			}
		}

		if( iniFile.exists( modelName, "initialRowValue" ) )	// The dim = 2
		{
			const Ini::IdList &rowsList( iniFile.definition(modelName, "initialRowValue" ) ) ;

			register unsigned row = 0;
			Real val;

			for ( Ini::IdList::const_iterator cursor = rowsList.begin(); cursor != rowsList.end(); cursor++ )
			{
				// the first value is the row number
				row = str2Int( (*cursor) ) ;
				MASSERTMSG( row < rows, "The number of row for initialRowValue is out of range. It's " + int2Str(row) + " and must be in [ 0, " + int2Str( rows-1 ) + "]" ) ;

				cursor++ ;

				MASSERTMSG( cursor != rowsList.end(), "Invalid initial row value for initialRowValue (must be a pair rowNumber rowValues)!" );

				// the second is the description of the row
				string rowVal( *cursor ) ;			

				MASSERTMSG( rowVal.size() == cols, "The size of the rows for the initial values of the CoupledCell must be equal to the width value !" );

				register unsigned col = 0;

				for (string::iterator rowCurs = rowVal.begin(); rowCurs != rowVal.end(); rowCurs++ )
				{
					if (*rowCurs >= '0' && *rowCurs <= '9')
						val.value( *rowCurs - '0');
					else
						val = Real::tundef;

					nTupla nt4;
					nt4.add(row, col);
					state[ nt4 ] = val;
					col++;
				}
			}
		}

		if( iniFile.exists( modelName, "initialCellsValue" ) )
		{
			string	fileName( iniFile.definition( modelName, "initialCellsValue" ).front() );
			FILE	*fileIn;
			char	line[250], pos[200], value[50];

			fileName = trimSpaces( fileName );
			fileIn = fopen( fileName.c_str(), "r" );
			MASSERTMSG( fileIn != NULL, "Can't open the file '" + fileName + "' defined by the initialCellsValue clause");

			while (!feof(fileIn))
			{
				fgets(line, 255, fileIn);
				if (line != NULL & splitLine(line, pos, value)){
					CellPosition	cp2(pos);
					state[ cp2 ] = str2Real(value);
				}
			}
			fclose( fileIn );
		}

		if( iniFile.exists( modelName, "initialMapValue" ) )
		{
			string	fileName( iniFile.definition( modelName, "initialMapValue" ).front() );
			FILE	*fileIn;
			char	line[250];

			fileName = trimSpaces( fileName );
			fileIn = fopen( fileName.c_str(), "r" );
			MASSERTMSG( fileIn != NULL, "Can't open the file '" + fileName + "' defined by the initialMapValue clause");

			CellPosition    counter( nt.dimension(), 0 );
			register bool   overflow = false;

			while (!overflow)
			{
				MASSERTMSG( !feof( fileIn ) && fgets(line, 255, fileIn), "Insuficient data in file specified with InitialMapValue");

				state[ counter ] = str2Real(line);

				overflow = counter.next( nt );
			}
			fclose( fileIn );
		}

		//////////////////////////////////////////////////////////////
		// Now, the initial state is already loaded.
		//////////////////////////////////////////////////////////////

		//////////////////////////////////////////////////////////////		
		// Open the logfiles and load the first lines of each
		//////////////////////////////////////////////////////////////		
		VTime currentTime( VTime::Inf ), nextShowTime( initial ), nextTime (VTime::Inf);
		istream **logStreams ;
		VTime *fileTimes;
		char **lines;

		int filecounter, filecount;

		if ( logName != "-" ) 
		{
			MASSERT(modelsLogFiles.exists( "logfiles" , modelName ) );

			const Ini::IdList &files( modelsLogFiles.definition( "logfiles", modelName ) ) ;

			filecount = files.size();

			logStreams = new istream*[filecount];
			lines= new char*[filecount];
			fileTimes = new VTime[filecount];

			Ini::IdList::const_iterator cursor = files.begin();
			for ( filecounter = 0; cursor != files.end() ; cursor++, filecounter++ ) 
			{
				logStreams[filecounter] = new fstream((*cursor).c_str(), ios::in);

				lines[filecounter] = new char[2048];
				lines[filecounter][0] = '\0';

				fileTimes[filecounter] = getNextMsgLine(*(logStreams[filecounter]), modelName, lines[filecounter], strPort);

				if ( fileTimes[filecounter] < currentTime )
					currentTime = fileTimes[filecounter];
			}

		}
		else
		{
			filecount = 1;
			logStreams = new istream*[1];
			lines = new char* [1];
			fileTimes = new VTime[1];
			logStreams[0] = new fstream("/dev/stdin", ios::in);
			lines[0] = new char[2048];
			lines[0][0] = '\0';

			fileTimes[0] = getNextMsgLine(*(logStreams[0]), modelName, lines[0], strPort);

			if ( fileTimes[0] < currentTime )
				currentTime = fileTimes[0];


		}


		//////////////////////////////////////////////////////////////		
		// All the files are now open
		//////////////////////////////////////////////////////////////		

		bool val = false ;
		int lineCount(1) ;
		CellPosition	nt5;

		//Initially, make nextTime = currentTime, to go through the loop at least once.
		nextTime = currentTime;

		do
		{
			// Acumulate changes till next show time
			while( nextTime <= nextShowTime )
			{
				//El nextTime deberia ser mayor o igual al currentTime
				MASSERT( currentTime <= nextTime);
				currentTime = nextTime;				

				//On every loop, calculate the new nextTime
				nextTime = VTime::Inf;

				for (filecounter = 0; filecounter < filecount; filecounter++)
				{

					if (fileTimes[filecounter] == currentTime)
					{
						val = parseLine( lines[filecounter], currentTime, modelName, nt5, value, strPort ) ;

						//Todas las lineas deberian ser validas
						MASSERT(val);

						state[ nt5 ] = value ;
						fileTimes[filecounter] = getNextMsgLine(*(logStreams[filecounter]), modelName, lines[filecounter], strPort);
					}	


					if ( fileTimes[filecounter] < nextTime )
						nextTime = fileTimes[filecounter];
				}//for

			}

			// Draw linenumber and time
			if (!Impresion::Default.FlatLog())
				cout << "Line : " << lineCount << " - " ;

			// All state printing delegated to this method
			printState( state, nextShowTime ) ;

			if ( timeInterval == VTime::InvalidTime )
				nextShowTime = currentTime ;
			else
				nextShowTime = nextShowTime + timeInterval;

		// When the are no more event issued, it means the next time will be infinity
		} while( !(nextTime == VTime::Inf) ) ;

		for( filecounter = 0; filecounter < filecount; filecounter++)
		{
			delete logStreams[filecounter];
			delete lines[filecounter];
		}

		delete logStreams;
		delete lines;
		delete fileTimes;

	} catch( MException &e )
	{
		e.print(cerr);
	} catch( ... )
	{
		cerr << "Unknown exception! " << endl ;
	}
}


/**********************************************************************
*getNextMsgLine: Stores in buffer the next valid msg line and returns the
*time of the message. If no valid line is found, it returns VTime::Inf
***********************************************************************/
VTime getNextMsgLine( istream& file, const string& modelName, char* buffer, const string &portName )
{
	bool valid;
	VTime time;
	CellPosition cellPos;
	Real value;

	valid = false;

	while ( !valid && file.good() && !file.eof() ) {
		file.getline( buffer, 2048 );
		valid = parseLine( buffer, time, modelName, cellPos, value, portName ) ;
	}

	if ( !valid )
		time = VTime::Inf;

	return time;
}

#include <assert.h>

#include <cstring>
#include <cstdlib>
#include <iostream>
#include <fstream>
#include <map>
#include <set>
#include <list>

using namespace std;

// RGB data of the image
static unsigned char g_image[480][320][3];

// The board is SIZE x SIZE tiles
#define SIZE 6

// The tile "bodies" information - filled by DetectTileBodies()
// via heuristics on the center pixel of the tile
//
enum TileKind {
    empty    = 0,
    block    = 1,
    prisoner = 2
};
static TileKind g_tiles[SIZE][SIZE];

// The top and bottom "borders" of each tile
// (hence the 2x in the vertical direction)
// filled by DetectTopAndBottomTileBorders()
//
enum BorderKind {
    notBorder = 0,
    white     = 1,
    black     = 2
};
static BorderKind g_borders[2*SIZE][SIZE];

// The board is a list of Blocks:
struct Block {
    static int BlockId; // class-global counter, used to...
    int _id;            // ...uniquely identify each block
    int _y, _x;         // block's top-left tile coordinates
    bool _isHorizontal; // whether the block is Horiz/Vert
    TileKind _kind;     // can only be block or prisoner
    int _length;        // how many tiles long this block is
    Block(int y, int x, bool isHorizontal, TileKind kind, int length):
        _id(BlockId++), _y(y), _x(x), _isHorizontal(isHorizontal),
        _kind(kind), _length(length)
        {}
};
int Block::BlockId = 0;

// This function (called at startup) scans the g_tiles and g_borders
// arrays, and understands where the blocks are.
//
// Returns a list of the detected Blocks
list<Block> ScanBodiesAndBordersAndEmitStartingPiecePositions()
{
    list<Block> pieces;
    bool isTileKnown[SIZE][SIZE];

    // Initially, we don't have a clue what each tile has
    memset(isTileKnown, false, sizeof(isTileKnown));
    while (true) {
        for(int y=0; y<SIZE; y++) {
            for(int x=0; x<SIZE; x++) {
                if (isTileKnown[y][x])
                    // Skip over tiles we already know
                    continue;

                if (empty == g_tiles[y][x]) {
                    // Skip over empty tiles
                    isTileKnown[y][x] = true;
                    continue;
                }
                bool isMarker = (g_tiles[y][x]==prisoner);
                const char *marker = isMarker?" (marker)":"";

                // Use the border information:
                if (g_borders[2*y][x] == white && 
                        g_borders[2*y+1][x] == black) {
                    // If a tile has white on top and black on bottom,
                    // then it is part of a horizontal block
                    isTileKnown[y][x] = true;
                    int xend = x+1;
                    // Scan horizontally to find its end
                    while(xend<SIZE && g_borders[2*y+1][xend] == black &&
                            g_borders[2*y][xend] == white) {
                        isTileKnown[y][xend] = true;
                        xend++;
                    }
                    // two adjacent blocks of length 2 would lead
                    // to a 'block' of length 4...
                    if (xend-x==4) {
                        // ...in that case, emit two blocks of length 2
                        cout << "Horizontal blocks at " << y << "," << x;
                        cout << " of length 2 " << marker << "\n";
                        pieces.push_back(
                            Block(y,x, true, g_tiles[y][x], 2));
                        pieces.push_back(
                            Block(y,x+2, true, g_tiles[y][x+2], 2));
                    } else {
                        // ... otherwise emit only one block
                        cout << "Horizontal block at " << y << "," << x;
                        cout << " of length " << xend-x << marker << "\n";
                        pieces.push_back(
                            Block(y,x, true, g_tiles[y][x], xend-x));
                    }
                } else if (g_borders[2*y][x] == white) {
                    // If a tile doesn't have white on top and black on bottom,
                    // then it is part of a vertical block
                    isTileKnown[y][x] = true;
                    int yend = y+1;
                    // Scan vertically to find its end
                    while(yend<SIZE && g_borders[2*yend+1][x] != black) {
                        isTileKnown[yend][x] = true;
                        yend++;
                    }
                    cout << "Vertical   block at " << y << "," << x;
                    cout << " of length " << yend-y+1 << marker << "\n";
                    pieces.push_back(
                        Block(y,x, false, g_tiles[y][x], yend-y+1));
                } else
                    // either an empty, or a body-of-block tile
                    isTileKnown[y][x] = true;
            }
        }
        bool allDone = true;
        for(int y=0; y<SIZE; y++)
            for(int x=0; x<SIZE; x++)
                allDone = allDone && isTileKnown[y][x];
        if (allDone)
            break;
    }
    return pieces;
}

// A board is indeed represented as a list of Blocks.
// However, when we move Blocks around, we need to be able
// to detect if a tile is empty or not - so a 2D representation
// (for quick tile access) is required.
struct Board {
    TileKind _data[SIZE*SIZE];
    // 2D access operator
    // i.e. instead of 'arr[y][x]' you do 'arr(y,x)'
    inline TileKind& operator()(int y, int x) {
        return _data[y*SIZE+x];
    }
    // This type is also used in both sets and maps as a key -
    // so it has a comparison operator.
    bool operator<(const Board& r) const {
        return memcmp(_data, r._data, sizeof(_data))<0;
    }
    // Initial state: set all tiles to empty
    Board() { memset(&_data, empty, sizeof(_data)); }
};

// This function takes a list of blocks, and 'renders' them
// into a Board - for quick tile access.
Board renderPieces(list<Block>& pieces)
{
    Board tmp;
    for(list<Block>::iterator it=pieces.begin(); it!=pieces.end(); it++) {
        Block& p = *it;
        if (p._isHorizontal)
            for(int i=0; i<p._length; i++)
                tmp(p._y, p._x+i) = p._kind;
        else
            for(int i=0; i<p._length; i++)
                tmp(p._y+i, p._x) = p._kind;
    }
    return tmp;
}

// This function pretty-prints a list of blocks
void printBoard(const list<Block>& pieces)
{
    unsigned char tmp[SIZE][SIZE];
    // start from an empty buffer
    memset(tmp, ' ', sizeof(tmp));
    list<Block>::const_iterator it=pieces.begin();
    for(; it!=pieces.end(); it++) {
        const Block& piece = *it;
        char c; // character emitted for this tile
        switch (piece._kind) {
        case empty:
            c = ' ';
            break;
        case prisoner:
            c = 'Z'; // Our Zorro tile :-)
            break;
        // ... and use a different letter for each block
        case block:
            c = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"[piece._id];
            break;
        }
        if (piece._isHorizontal)
            for(int i=0; i<piece._length; i++)
                tmp[piece._y][piece._x+i] = c;
        else
            for(int i=0; i<piece._length; i++)
                tmp[piece._y+i][piece._x] = c;
    }

    cout << "+------------------+\n|";
    for(int i=0; i<36; i++) {
        char c = tmp[i/SIZE][i%SIZE];
        cout << c << c << " ";
        if (5 == (i%SIZE)) {
            if (i == 17) cout << " \n|"; // Freedom path
            else         cout << "|\n|"; // walls
        }
    }
    cout << "\b+------------------+\n";
}

// When we find the solution, we also need to backtrack
// to display the moves we used to get there...
//
// "Move" stores what block moved and to what direction
struct Move {
    int _blockId;
    enum Direction {left, right, up, down} _move;
    Move(int blockID, Direction d):
        _blockId(blockID),
        _move(d) {}
};

// Utility function - we need to be able to "deep copy"
// a list of Blocks, to form alternate board states (see SolveBoard)
list<Block> copyPieces(const list<Block>& pieces)
{
    list<Block> copied;
    for(list<Block>::const_iterator it=pieces.begin(); it!=pieces.end(); it++)
        copied.push_back(*it);
    return copied;
}

// The brains of the operation - basically a Breadth-First-Search
// of the problem space:
//    http://en.wikipedia.org/wiki/Breadth-first_search
//
void SolveBoard(list<Block>& pieces)
{
    cout << "\nSearching for a solution...\n";

    // We need to store the last move that got us to a specific
    // board state - that way we can backtrack from a final board
    // state to the list of moves we used to achieve it.
    map< Board, Move> previousMoves;
    // Start by storing a "sentinel" value, for the initial board
    // state - we used no Move to achieve it, so store a block id
    // of -1 to mark it:
    previousMoves.insert(
        pair<Board,Move>(renderPieces(pieces), Move(-1, Move::left)));

    // We must not revisit board states we have already examined,
    // so we need a 'visited' set:
    set<Board> visited;

    // Now, to implement Breadth First Search, all we need is a Queue
    // storing the states we need to investigate - so it needs to
    // be a list of board states... i.e. a list of list of Blocks!
    list< list<Block> > queue;

    // Start with our initial board state
    queue.push_back(pieces);
    while(!queue.empty()) {

        // Extract first element of the queue
        list<Block> pieces = *queue.begin();
        queue.pop_front();

        // Create a Board for fast 2D access to tile state
        Board board = renderPieces(pieces);

        // Have we seen this board before?
        if (visited.find(board) != visited.end())
            // Yep - skip it
            continue;

        // No, we haven't - store it so we avoid re-doing
        // the following work again in the future...
        visited.insert(board);

        // Check if this board state is a winning state:
        // Find prisoner block...
        list<Block>::iterator it=pieces.begin();
        for(; it!=pieces.end(); it++) {
            Block& piece = *it;
            if (piece._kind == prisoner)
                break;
        }
        assert(it != pieces.end()); // The prisoner is always there!

        // Can he escape? Check to his right!
        bool allClear = true;
        for (int x=it->_x+it->_length; x<SIZE ; x++) {
            allClear = allClear && !board(it->_y, x);
            if (!allClear)
                break;
        }
        if (allClear) {
            // Yes, he can escape - we did it!
            cout << "Solved!\n";

            // To print the Moves we used in normal order, we will
            // backtrack through the board states to print
            // the Move we used at each one...
            list<list<Block> > solution;
            solution.push_front(copyPieces(pieces));

            map<Board,Move>::iterator itMove = previousMoves.find(board);
            while (itMove != previousMoves.end()) {
                if (itMove->second._blockId == -1)
                    // Sentinel - reached starting board
                    break;
                // Find the block we moved, and move it
                // (in reverse direction - we are going back)
                for(it=pieces.begin(); it!=pieces.end(); it++) {
                    if (it->_id == itMove->second._blockId) {
                        switch(itMove->second._move) {
                        case Move::left:  it->_x++; break;
                        case Move::right: it->_x--; break;
                        case Move::up:    it->_y++; break;
                        case Move::down:  it->_y--; break;
                        }
                        break;
                    }
                }
                assert(it != pieces.end());
                // Add this board to the front of the list...
                solution.push_front(copyPieces(pieces));
                board = renderPieces(pieces);
                itMove = previousMoves.find(board);
            }
            // Now that we have the full list, emit it in order
            for(list<list<Block> >::iterator itState=solution.begin();
                itState != solution.end(); itState++)
            {
                printBoard(*itState);
                cout << "Press ENTER for next move\n";
                cin.get();
            }
            cout << "Run free, prisoner, run! :-)\n";
            exit(0);
        }

        // Nope, the prisoner is still trapped.
        //
        // Add all potential states arrising from immediate
        // possible moves to the end of the queue.
        for(it=pieces.begin(); it!=pieces.end(); it++) {
            Block& piece = *it;

#define COMMON_BODY(direction) \
    list<Block> copied = copyPieces(pieces);                  \
    /* Add to the end of the queue for further study :-) */   \
    queue.push_back(copied);                                  \
    /* Store board and move, so we can backtrack later */     \
    previousMoves.insert(                                     \
        pair<Board,Move>(                                     \
            renderPieces(copied),                             \
            Move(piece._id, Move::direction)));

            if (piece._isHorizontal) {
                // Can the block move to the left?
                if (piece._x>0 && 
                        empty==board(piece._y, piece._x-1)) {
                    piece._x--;
                    COMMON_BODY(left)
                    piece._x++;
                }
                // Can the block move to the right?
                if (piece._x+piece._length<SIZE && 
                        empty==board(piece._y, piece._x+piece._length)) {
                    piece._x++;
                    COMMON_BODY(right)
                    piece._x--;
                }
            } else {
                // Can the block move up?
                if (piece._y>0 && 
                        empty==board(piece._y-1, piece._x)) {
                    piece._y--;
                    COMMON_BODY(up)
                    piece._y++;
                }
                // Can the block move down?
                if (piece._y+piece._length<SIZE && 
                        empty==board(piece._y + piece._length, piece._x)) {
                    piece._y++;
                    COMMON_BODY(down)
                    piece._y--;
                }
            }
        }
        // and go recheck the queue, from the top!
    }
}

void DetectTileBodies()
{
    // This function looks at the center pixel of each tile,
    // and guesses what TileKind it is.
    //
    // (Heuristics on the snapshots taken from my iPhone)
    //
    cout << "Detecting tile bodies...\n";
    for(int y=0; y<SIZE; y++) {
        for(int x=0; x<SIZE; x++) {
            unsigned line   = 145 + y*50;
            unsigned column =  34 + x*50;
            // The red channel, surprisingly, was not necessary
            //unsigned char r = g_image[line][column][0];
            unsigned char g = g_image[line][column][1];
            unsigned char b = g_image[line][column][2];
            if (b > 30)
                g_tiles[y][x] = empty;
            else if (g < 30)
                g_tiles[y][x] = prisoner;
            else
                g_tiles[y][x] = block;
        }
    }
}

void DetectTopAndBottomTileBorders()
{
    cout << "Detecting top and bottom tile borders...\n\n";
    for(int y=0; y<SIZE; y++) {
        for(int x=0; x<SIZE; x++) {
            unsigned line    = 145 + y*50;
            unsigned column  =  34 + x*50;
            unsigned ytop    = line - 23;
            unsigned ybottom = line + 23;

            unsigned char r = g_image[ytop][column][0];
            unsigned char g = g_image[ytop][column][1];
            //unsigned char b = g_image[ytop][column][2];
            if      (r > 200 && g > 160) g_borders[y*2][x] = white;
            else if (r < 40 && g < 30)   g_borders[y*2][x] = black;
            else                         g_borders[y*2][x] = notBorder;

            r = g_image[ybottom][column][0];
            g = g_image[ybottom][column][1];
            //b = g_image[ybottom][column][2];
            if      (r > 200 && g > 160) g_borders[y*2+1][x] = white;
            else if (r < 40 && g < 30)   g_borders[y*2+1][x] = black;
            else                         g_borders[y*2+1][x] = notBorder;
        }
    }
}

int main()
{
    ifstream rgbDataFileStream;
    rgbDataFileStream.open("data.rgb", ios::in | ios::binary);
    if (!rgbDataFileStream.is_open()) {
        cerr << "Convert your iPhone snapshot to 'data.rgb' ";
        cerr << "with ImageMagick:\n\n";
        cerr << "\tbash$ convert IMG_0354.PNG data.rgb\n\n";
        exit(1);
    }
    rgbDataFileStream.read(
        reinterpret_cast<char*>(&g_image[0][0][0]),
        480*320*3);
    if (rgbDataFileStream.fail() || rgbDataFileStream.eof()) {
        cerr << "Failed to read 480x320x3 bytes from 'data.rgb'...\n\n";
        exit(1);
    }
    rgbDataFileStream.close();
    DetectTileBodies();
    DetectTopAndBottomTileBorders();
    list<Block> pieces =
        ScanBodiesAndBordersAndEmitStartingPiecePositions();
    SolveBoard(pieces);
}

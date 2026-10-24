// The game in a view, for the level editor's play-test: one for the whole app (one Vulkan instance a process), moved
// between the screens that show it. It plays a level as the editor has it (its text and grids, a catalog of its own:
// LevelCatalog.h) from a start, the player's fingers or the bot playing, as fast as asked.
#import <UIKit/UIKit.h>

NS_ASSUME_NONNULL_BEGIN

@interface CSEngineView : UIView

+ (instancetype)shared;

/// Plays level `number` as `text` reads (grids: name -> rows, one a line, standing in for theirs), from `startX` metres
/// along (-1: the level's start). NO, and why in *error, if it doesn't read or the graphics won't start.
- (BOOL)playLevel:(int)number text:(NSString*)text grids:(NSDictionary<NSString*, NSString*>*)grids startX:(double)startX bot:(BOOL)bot
            error:(NSString* _Nullable* _Nullable)error;
/// Plays level `number` from inside its maze: its feet in cell (`col`, `row`: the game's row, 0 at the street), running
/// `dir` (1 right, -1 left); col -1: from the maze's door
- (BOOL)playMaze:(int)number text:(NSString*)text grids:(NSDictionary<NSString*, NSString*>*)grids col:(int)col row:(int)row dir:(int)dir
             bot:(BOOL)bot error:(NSString* _Nullable* _Nullable)error;
/// the same run again, from the same start
- (void)restart;
/// off (its frames stop), and on again
- (void)pause;
- (void)resume;

@property (nonatomic) BOOL bot;            // the bot plays (it can take over mid-run, and hand back)
@property (nonatomic) NSInteger speed;     // game updates a frame: 1, 2 or 4 (fast-forward)
@property (nonatomic, readonly) NSInteger runState; // 0 not started, 1 playing, 2 over
@property (nonatomic, readonly) BOOL cleared;
@property (nonatomic, readonly) int level;
@property (nonatomic, readonly) double henX;   // metres along
@property (nonatomic, readonly) double henY;   // the floor her stack's on, metres
@property (nonatomic, readonly) NSInteger eggs;
@property (nonatomic, readonly) double seconds; // this run's
/// her way this run: (x, the top of her stack, metres) every few frames
@property (nonatomic, readonly) NSArray<NSValue*>* trace;
/// a run over: cleared or not, where she got to
@property (nonatomic, copy, nullable) void (^onRunEnded)(BOOL cleared, double x, double y);
@property (nonatomic, readonly, copy) NSString* graphicsError; // empty while it's fine
/// the safe area the game lays its HUD out by: an iPhone's (its notch, its home bar) when it plays in a phone-sized
/// frame on an iPad; UIEdgeInsetsZero: the view's own
@property (nonatomic) UIEdgeInsets phoneInsets;

@end

NS_ASSUME_NONNULL_END

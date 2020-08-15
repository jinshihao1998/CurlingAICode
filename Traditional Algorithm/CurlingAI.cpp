#include <stdio.h>
#include <cstring >
#include <string>
#include <string.h>
#include "stdio.h"
#include <iostream>
#include <fstream>
#include <algorithm> 
#include <stdlib.h>
#include <assert.h>
#include<winsock.h>
#include<math.h>
#include<time.h>
#include<vector>
#include<windows.h>

#define _CRT_SECURE_NO_WARNINGS
#pragma warning(disable:4996)
#pragma comment(lib,"ws2_32.lib")

#ifdef _WIN32
#include <Windows.h>
#endif

using namespace std;
static const int BUFSIZE = 1024;
static SOCKET m_server;
static const char* IP = "127.0.0.1";
static const int PORT = 7788;

typedef struct _GAMESTATE {
	int		ShotNum;		// number of Shot
							// if ShotNum = n, next Shot is (n+1)th shot in this End
	int		CurEnd;			// (number of current end) - 1， 当前是第几局
	int		LastEnd;		// number of final End ，初赛4局，决赛8局
	int		Score[8];		// score of each End (if Score < 0: First player in 1st End scored)
	bool	WhiteToMove;	// Which player will shot next
							// if WhiteToMove = 0: First player in 1st End will shot next, 
							//  else (WhiteToMove = 1) : Second player will shot next

	double	body[16][2];	// body[n][0] : x of coordinate of n th stone
							// body[n][1] : y of coordinate of n th stone
							// 每队8个，共16个冰壶

}GAMESTATE, * PGAMESTATE;

typedef struct _ShotInfo
{
	_ShotInfo(float s, float h, float a)
	{
		speed = s;
		h_x = h;
		angle = a;
	};
	float speed;
	float h_x;
	float angle;
}SHOTINFO;

typedef struct _MOTIONINFO
{
	float x_coordinate;
	float y_coordinate;
	float x_velocity;
	float y_velocity;
	float angular_velocity;
}MOTIONINFO;

// state of the game
GAMESTATE GameState;
SHOTINFO shot(0.0f, 0.0f, 0.0f);
MOTIONINFO motionInfo;
double sweepDistance;

typedef struct _Ball
{
	float x_coordinate;
	float y_coordinate;
	float dist_from_center;
	int order; // 冰壶的顺序
}Ball;

Ball ball[16];
vector<Ball> firstHandInCenter;
vector<Ball> secondHandInCenter;
void shotForCenter(double x, double y);

// positions on sheet
static const float TEE_X = (float)2.375;    // x of center of house
static const float TEE_Y = (float)4.880;    // y of center of house,大本营坐标
static const float HOUSE_R0 = (float)0.15;  
static const float HOUSE_R1 = (float)0.61;
static const float HOUSE_R2 = (float)1.22;
static const float HOUSE_R = (float)1.870;  // radius of house 大本营半径
static const float STONE_R = (float)0.145;  // radius of stone 球半径

// coordinate (x, y) is in play-area if:
// (PLAYAREA_X_MIN < x < PLAYAREA_X_MAX && PLAYAREA_Y_MIN < y < PLAYAREA_Y_MAX)
static const float PLAYAREA_X_MIN = (float)0.000 + STONE_R;
static const float PLAYAREA_X_MAX = (float)0.000 + (float)4.750 - STONE_R;
static const float PLAYAREA_Y_MIN = (float)2.650 + STONE_R;
static const float PLAYAREA_Y_MAX = (float)2.650 + (float)8.165 - STONE_R;
/* 大概v = 2.385 y = 10.5160 自由防守区边缘     */
/* 大概v = 2.85 y = 6.1052 中区进营靠前位置     */
/* 大概v = 2.99 y = 4.880 营垒最中间            */
/* 大概v = 2.7 y = 7.5506 自由防护区中区靠后位置*/
/* 大概v = 2.6 y = 8.4710 自由防护区中区中间位置*/

static const float x_shot_error = 0.0242875;    // 直线发射的横向位置偏差
static const float EPISON = 1e-6;               // 定义非常小的数
static const float STONE_D = 2 * STONE_R;
static const float PI = 3.141592653;
static const float protect_dist = 4.0;          // 保护球的距离
static const float center_dist = 1.25 * STONE_D; // 判断中心的标志
static const float center_dist2 = HOUSE_R1 - STONE_R;


//  get distance^2 from center of House
double distFromCenter(double x, double y){
	return sqrt(pow(x - TEE_X, 2) + pow(y - TEE_Y, 2));
}

bool compare(Ball n1, Ball n2) {
	return n1.dist_from_center < n2.dist_from_center;
}

int sumScore() {
	/*返回当前真正的分数*/
	int s = 0;
	for (int i = 0; i < 8; i++) {
		s = s + GameState.Score[i];
	}
	return s;
}

void sortDist() {
	/*将球按距离排序并储存在ball结构体数组*/
	for (int i = 0; i < 16; i++) {
		ball[i].x_coordinate = GameState.body[i][0];
		ball[i].y_coordinate = GameState.body[i][1];
		ball[i].order = i;
		ball[i].dist_from_center = distFromCenter(GameState.body[i][0], GameState.body[i][1]);
	}
	sort(ball, ball + 16, compare);
}

int currentScoreInCenter() {
	/* 返回当前本垒状态中，先手得分*/
	int curScore = 0;
	if (ball[0].dist_from_center < HOUSE_R + STONE_R) {
		if (ball[0].order % 2 == 0) {
			curScore++;
			for (int i = 1; i < 16; i++) {
				if (ball[i].dist_from_center < HOUSE_R + STONE_R &&
					ball[i].order % 2 == 0) {
					curScore++;
				}
				else {
					break;
				}
			}
		}
		else {
			curScore--;
			for (int i = 1; i < 16; i++) {
				if (ball[i].dist_from_center < HOUSE_R + STONE_R &&
					ball[i].order % 2 == 1) {
					curScore--;
				}
				else {
					break;
				}
			}
		}
	}
	return curScore;
}

void ballInCenterFunc() {
	/*获得当前本垒中先手 后手的球*/
	firstHandInCenter.clear();
	secondHandInCenter.clear();
	for (int i = 0; i < 16; i++) {
		if (ball[i].dist_from_center < HOUSE_R + STONE_R) {
			// 压边也算入营
			if (ball[i].order % 2 == 0) {
				firstHandInCenter.push_back(ball[i]);
			}
			else {
				secondHandInCenter.push_back(ball[i]);
			}
		}
		else {
			break;
		}
	}
}

int numberOfADistance(double s) {
	int sum_num = 0;
	for (int i = 0; i < 16; i++) {
		if (distFromCenter(GameState.body[i][0], GameState.body[i][1]) < s) {
			sum_num++;
		}
	}
	return sum_num;
}

double speedAtFixedDistance(double y) {
	/* 给定y来求解需要的速度v                  */
	/* v与y一次关系拟合：v = -0.1118y + 3.5378 */
	return (sqrt(-0.6324 * y + 12.047) + sqrt(-0.5912 * y + 11.768) + (-0.1118 * y + 3.5378)) / 3;
}

bool ShengJin(double a, double b, double c, double d, double x_err)
{
	vector<double> X123;
	/* 盛金公式求解三次方程的解                  */
	/* 德尔塔f=B^2-4AC                           */
	/* 这里只要了实根，虚根需要自己再整理下拿出来*/
	double A = b * b - 3 * a * c;
	double B = b * c - 9 * a * d;
	double C = c * c - 3 * b * d;
	double f = B * B - 4 * A * C;
	double i_value;
	double Y1, Y2;
	bool flag = false;
	if (fabs(A) < 1e-6 && fabs(B) < 1e-6)//公式1
	{
		X123.push_back(-b / (3 * a));
		X123.push_back(-b / (3 * a));
		X123.push_back(-b / (3 * a));
	}
	else if (fabs(f) < 1e-6)   //公式3
	{
		double K = B / A;
		X123.push_back(-b / a + K);
		X123.push_back(-K / 2);
		X123.push_back(-K / 2);
	}
	else if (f > 1e-6)      //公式2
	{
		Y1 = A * b + 3 * a * (-B + sqrt(f)) / 2;
		Y2 = A * b + 3 * a * (-B - sqrt(f)) / 2;
		double Y1_value = (Y1 / fabs(Y1)) * pow((double)fabs(Y1), 1.0 / 3);
		double Y2_value = (Y2 / fabs(Y2)) * pow((double)fabs(Y2), 1.0 / 3);
		X123.push_back((-b - Y1_value - Y2_value) / (3 * a));
		//虚根我不要
		//虚根还是看看吧，如果虚根的i小于0.1，则判定为方程的一根吧。
		i_value = sqrt(3.0) / 2 * (Y1_value - Y2_value) / (3 * a);
		if (fabs(i_value) < 1e-1)
		{
			X123.push_back((-b + 0.5 * (Y1_value + Y2_value)) / (3 * a));
		}
	}
	else if (f < -1e-6)   //公式4
	{
		double T = (2 * A * b - 3 * a * B) / (2 * A * sqrt(A));
		double S = acos(T);
		X123.push_back((-b - 2 * sqrt(A) * cos(S / 3)) / (3 * a));
		X123.push_back((-b + sqrt(A) * (cos(S / 3) + sqrt(3.0) * sin(S / 3))) / (3 * a));
		X123.push_back((-b + sqrt(A) * (cos(S / 3) - sqrt(3.0) * sin(S / 3))) / (3 * a));
	}
	if (x_err > 0) {
		for (auto t : X123) {
			if (t > -10 && t < 0) {
				shot.angle = t;
				flag = true;
			}
		}
	}
	else {
		for (auto t : X123) {
			if (t > 0 && t < 10) {
				shot.angle = t;
				flag = true;
			}
		}
	}
	return flag;
}

bool solveForVW(double y_coordinate, double x_err) {
	//投出曲线球
	double a, b, c, d;
	if (x_err > 0) {
		// w 为负 左旋
		a = -0.1118 * 0.0067 * 0.0712;
		b = -0.0024 * 0.1118 * 0.0712;
		c = ((0.0275 + y_coordinate) * 0.1118 - 3.5378) * 0.0712 + 0.0747;
		d = -x_err + 0.071375;
		if (ShengJin(a, b, c, d, x_err)) {
			shot.speed = (((x_err - 0.071375) / shot.angle) - 0.0747) / (-0.0712);
			if (shot.speed > 0.0 && shot.speed < 10.0) {
				return true;
			}
		}
	}
	else {
		// w为正 右旋
		a = -0.0058 * 0.1118 * 0.0696;
		b = -0.0078 * 0.1118 * 0.0696;
		c = ((0.0373 + y_coordinate) * 0.1118 - 3.5378) * 0.0696 + 0.0702;
		d = -x_err - 0.02205;
		if (ShengJin(a, b, c, d, x_err)) {
			shot.speed = (((x_err + 0.02205) / shot.angle) - 0.0702) / (-0.0696);
			if (shot.speed > 0.0 && shot.speed < 10.0) {
				return true;
			}
		}
	}
	return false;
}

vector<vector<int>> distributionAroundABall(double x_coordinate, double y_coordinate) {
	/*返回一个位置左下边球的标号 右下边球的标号*/
	/*返回一个位置正下面球的标号 正上面球的标号*/
	/*注意越往下纵坐标越大                     */
	vector<vector<int>> A(4);
	for (int i = 0; i < 16; i++) {
		if (GameState.body[i][0] < (x_coordinate - 2.0*STONE_R) && GameState.body[i][1] > (y_coordinate + STONE_R)) {
			// 位于球的左下面
			A[0].push_back(i);
		}
		else if (GameState.body[i][0] > (x_coordinate + 2.0 * STONE_R) && GameState.body[i][1] > (y_coordinate + STONE_R)) {
			// 位于球的右下面
			A[1].push_back(i);
		}
		else if (GameState.body[i][0] >= (x_coordinate - 2.0 * STONE_R) && GameState.body[i][0] <= (x_coordinate + 2.0 * STONE_R) &&
			GameState.body[i][1] > y_coordinate + STONE_R) {
			// 位于球的正下面
			A[2].push_back(i);
		}
		else if (GameState.body[i][0] >= (x_coordinate - 2.0 * STONE_R) && GameState.body[i][0] <= (x_coordinate + 2.0 * STONE_R) &&
			GameState.body[i][1] < y_coordinate - STONE_R) {
			// 位于球的正上面
			A[3].push_back(i);
		}
	}
	return A;
}

bool shotForStraightBall(double x_coordinate, double y_coordinate) {
	/*投直线球到达x y的位置      */
	/*当直线轨迹有球时 返回false*/
	vector<vector<int>> distribution;
	distribution = distributionAroundABall(x_coordinate, y_coordinate);
	if (distribution[2].size() == 0) {
		shot.speed = speedAtFixedDistance(y_coordinate);
		shot.angle = 0;
		shot.h_x = x_coordinate - TEE_X - x_shot_error;
		return true;
	}
	else {
		return false;
	}
}

int IfOccupied(double x, double y) {
	/*返回纵向最近球的标号*/
	/*若其下无球，返回-1  */
	vector<vector<int>> distribution;
	distribution = distributionAroundABall(x, y);
	if (distribution[2].size() == 0) {
		return -1;
	}
	else {
		double y_dist = 1e6;
		int j = -1;
		for (auto i : distribution[2]) {
			double tmpx = GameState.body[i][0];
			double tmpy = GameState.body[i][1];
			if (GameState.body[i][1] - y < y_dist && 
				sqrt(pow(tmpx - x, 2) + pow(tmpy - y, 2)) > 1.5 * STONE_D) {
				// 距离其本身有一定的距离
				j = i;
				y_dist = GameState.body[i][1] - y;
			}
		}
		return j;
	}
}

int IfOccupied2(double x, double y) {
	/*返回纵向最远球的标号*/
	/*若其下较远处无球，返回-1  */
	vector<vector<int>> distribution;
	distribution = distributionAroundABall(x, y);
	if (distribution[2].size() == 0) {
		return -1;
	}
	else {
		double y_dist = 1e-6;
		int j = -1;
		for (auto i : distribution[2]) {
			if (GameState.body[i][1] - y > y_dist) {
				j = i;
				y_dist = GameState.body[i][1] - y;
			}
		}
		double tmpx = GameState.body[j][0];
		double tmpy = GameState.body[j][1];
		if (sqrt(pow(tmpx - x, 2) + pow(tmpy - y, 2)) > 2*STONE_D) {
			// 距离其本身有一定的距离
			return j;
		}
		else {
			return -1;
		}
	}
}

bool trajectoryJudgment(double k, double b, double x_coordinate, double y_coordinate) {
	/*近似判断是否有球在轨迹上*/
	/*刨去其本身              */
	/*无球返回true            */
	//if (distFromCenter(x_coordinate, y_coordinate) > HOUSE_R1) {
	//	// 红色区域外不适用
	//	return true;
	//}
	for (int i = 0; i < 16; i++) {
		double tmpx, tmpy;
		tmpx = GameState.body[i][0] - x_coordinate;
		tmpy = GameState.body[i][1] - y_coordinate;
		if (sqrt(pow(tmpx, 2) + pow(tmpy, 2)) > STONE_R &&
			fabs(k * tmpx - tmpy + b) / sqrt(pow(k, 2) + 1) < STONE_D
			&& tmpy > 0) {
			return false;
		}
	}
	return true;
}

bool shotForCurveBall(double x_coordinate, double y_coordinate) {
	/* 给定坐标 发射曲线球 曲线贴或者曲线定位    */
	/* 若有球阻挡 则返回false                    */
	/* w最大为10时 纵坐标越小 x_err绝对值越大    */
	/* 可调节的参数：tmpx_err  y_dist_max overlap*/
	double x_err; // 初始横坐标与目标横坐标的差值
	vector<vector<int>> distribution;
	distribution = distributionAroundABall(x_coordinate, y_coordinate);
	if (distribution[2].size() == 0) {
		// 如果坐标的正下方没有球
		return shotForStraightBall(x_coordinate, y_coordinate);
	}
	else {
		// 如果球的正下方有球
		// 离他最近得球最可能碰到
		// 判断离y_coordinate最近的球的距离 是否足够远
		double y_dist;
		int j = 0;
		double tmpx_err = 1.3;
		double y_dist_max = 4.2;
		// 获取下方与它最近的球的距离
		if (IfOccupied(x_coordinate, y_coordinate) == -1) {
			y_dist = 1e6;
		}
		else {
			j = IfOccupied(x_coordinate, y_coordinate);
			y_dist = GameState.body[j][1] - y_coordinate;
		}

		if (y_dist < y_dist_max) {
			// 如果不是足够远，则不能旋到准确位置，有一定偏差
			// 确定重合的系数overlap
			double overlap = -0.36*y_dist + 1.522;
			if (overlap > 1) {
				// 下方的球靠的很近 肯定碰上
				return false;
			}
			if (fabs(GameState.body[j][0] - x_coordinate) < 0.015) {
				if (rand() % 2 == 0) {
					// 从左边打
					x_coordinate = x_coordinate - overlap * STONE_R;
					if (x_coordinate - tmpx_err > PLAYAREA_X_MIN) {
						shot.h_x = x_coordinate - tmpx_err;
					}
					else {
						x_coordinate = x_coordinate + overlap * STONE_R;
						if (x_coordinate + tmpx_err < PLAYAREA_X_MAX) {
							shot.h_x = x_coordinate + tmpx_err;
						}
						else {
							shot.h_x = PLAYAREA_X_MAX;
						}
						double k = -0.699 * y_coordinate + 19.552;
						double b = -0.56183;
						if (!trajectoryJudgment(k, b, x_coordinate, y_coordinate)) {
							return false;
						}
					}
					double k = -1.0175 * y_coordinate + 20.546;
					double b = -0.5891;
					// 注意-k
					if (!trajectoryJudgment(-k, b, x_coordinate, y_coordinate)) {
						// 从右边打
						x_coordinate = x_coordinate + overlap * STONE_R;
						if (x_coordinate + tmpx_err < PLAYAREA_X_MAX) {
							shot.h_x = x_coordinate + tmpx_err;
						}
						else {
							shot.h_x = PLAYAREA_X_MAX;
						}
						double k = -0.699 * y_coordinate + 19.552;
						double b = -0.56183;
						if (!trajectoryJudgment(k, b, x_coordinate, y_coordinate)) {
							return false;
						}
					}
				}
				else {
					// 从右边打
					x_coordinate = x_coordinate + overlap * STONE_R;
					if (x_coordinate + tmpx_err < PLAYAREA_X_MAX) {
						shot.h_x = x_coordinate + tmpx_err;
					}
					else {
						x_coordinate = x_coordinate - overlap * STONE_R;
						if (x_coordinate - tmpx_err > PLAYAREA_X_MIN) {
							shot.h_x = x_coordinate - tmpx_err;
						}
						else {
							shot.h_x = PLAYAREA_X_MIN;
						}
						double k = -1.0175 * y_coordinate + 20.546;
						double b = -0.5891;
						// 注意-k
						if (!trajectoryJudgment(-k, b, x_coordinate, y_coordinate)) {
							return false;
						}
					}
					double k = -0.699 * y_coordinate + 19.552;
					double b = -0.56183;
					if (!trajectoryJudgment(k, b, x_coordinate, y_coordinate)) {
						// 从左边打
						x_coordinate = x_coordinate - overlap * STONE_R;
						if (x_coordinate - tmpx_err > PLAYAREA_X_MIN) {
							shot.h_x = x_coordinate - tmpx_err;
						}
						else {
							shot.h_x = PLAYAREA_X_MIN;
						}
						double k = -1.0175 * y_coordinate + 20.546;
						double b = -0.5891;
						// 注意-k
						if (!trajectoryJudgment(-k, b, x_coordinate, y_coordinate)) {
							return false;
						}
					}
				}
			}
			else {
				if (GameState.body[j][0] > x_coordinate) {
					x_coordinate = x_coordinate - overlap * STONE_R;
					if (x_coordinate - tmpx_err > PLAYAREA_X_MIN) {
						shot.h_x = x_coordinate - tmpx_err;
					}
					else {
						shot.h_x = PLAYAREA_X_MIN;
					}
					double k = -1.0175 * y_coordinate + 20.546;
					double b = -0.5891;
					// 注意-k
					if (!trajectoryJudgment(-k, b, x_coordinate, y_coordinate)) {
						return false;
					}
				}
				else {
					// 从右边打
					x_coordinate = x_coordinate + overlap * STONE_R;
					if (x_coordinate + tmpx_err < PLAYAREA_X_MAX) {
						shot.h_x = x_coordinate + tmpx_err;
					}
					else {
						shot.h_x = PLAYAREA_X_MAX;
					}
					double k = -0.699 * y_coordinate + 19.552;
					double b = -0.56183;
					if (!trajectoryJudgment(k, b, x_coordinate, y_coordinate)) {
						return false;
					}
				}
			}
		}
		else {
			// 如果足够远就可以旋到x_coordinate, y_coordinate
			// 先判断从左向右旋 再判断从右向左旋
			if (x_coordinate - tmpx_err > PLAYAREA_X_MIN) {
				shot.h_x = x_coordinate - tmpx_err;
			}
			else {
				if (x_coordinate + tmpx_err < PLAYAREA_X_MAX) {
					shot.h_x = x_coordinate + tmpx_err;
				}
				else {
					shot.h_x = PLAYAREA_X_MAX;
				}
				double k = -0.699 * y_coordinate + 19.552;
				double b = -0.56183;
				if (!trajectoryJudgment(k, b, x_coordinate, y_coordinate)) {
					return false;
				}
			}
			double k = -1.0175 * y_coordinate + 20.546;
			double b = -0.5891;
			if (!trajectoryJudgment(-k, b, x_coordinate, y_coordinate)) {
				// 从右边打向左旋x_err为正, w为负
				if (x_coordinate + tmpx_err < PLAYAREA_X_MAX) {
					shot.h_x = x_coordinate + tmpx_err;
				}
				else {
					shot.h_x = PLAYAREA_X_MAX;
				}
				double k = -0.699 * y_coordinate + 19.552;
				double b = -0.56183;
				if (!trajectoryJudgment(k, b, x_coordinate, y_coordinate)) {
					return false;
				}
			}
		}
		x_err = shot.h_x - x_coordinate;
		shot.h_x = shot.h_x - TEE_X;
	}
	// v与y的关系
	/* 用二次关系拟合：v = sqrt(-0.6324*y+12.047)         */
	/* 用一次关系拟合：v = -0.1118y + 3.5378              */

	// 向左旋
	/* x_err：初始横坐标 - 目标横坐标                     */
	/* x_err = (-0.0712v + 0.0747)w + 0.071375            */
	/* y_err: 此速度直线到达的y - 真实的偏转之后的y       */
	/* 一次的关系：y_err = -0.064275*|w| + 0.1375         */
	/* 二次的关系：y_err = -0.0067w^2 + 0.0024|w| + 0.0257*/
	/* 由此反解出v w                                      */
	/* x_err 为正 w为负 向左偏，x_err为负 w为正 向右偏    */

	//向右旋
	/*x_err = (-0.0696v + 0.0702)w - 0.02205*/
	/*y_err = -0.0058w^2 - 0.0078w + 0.0373 */
	if (solveForVW(y_coordinate, x_err)) {
		return true;
	}
	else {
		// 若没有找到w的解(修改参数后一般有解)
		return false;
	}
}

void randBall() {
	/*产生本垒中一定范围内的随机球*/
	/*产生随机球可以防止球聚拢    */
	/*可调参数：n                 */
	int n = 7;
	double x, y, t1, t2;
	for (;;) {
		t1 = rand() % int(2 * n * STONE_R * 100);
		t2 = rand() % int(2 * n * STONE_R * 100);
		x = t1 / 100.0 + TEE_X - n * STONE_R;
		y = t2 / 100.0 + TEE_Y - n * STONE_R;
		// 产生 TEE_X-n*STONE_R < x < TEE_X+n*STONE_R  TEE_Y-n*STONE_R < y < TEE_Y+n*STONE_R
		if (shotForCurveBall(x, y)) {
			break;
		}
	}
}

void launchHighSpeedBall(double x) {
	/*发射高速碰撞的球*/
	if (fabs(x - TEE_X) < HOUSE_R1 - STONE_R) {
		// 如果靠近本垒中线 打定
		shot.speed = 6;
		shot.angle = 0;
		shot.h_x = x - TEE_X + 0.02;
	}
	else if (x - TEE_X > 0) {
		// 靠右向左打甩
		shot.speed = 6;
		shot.angle = 0;
		shot.h_x = x - TEE_X - 0.025;
	}
	else {
		// 靠左向右打甩
		shot.speed = 6;
		shot.angle = 0;
		shot.h_x = x - TEE_X + 0.075;
	}
}

bool judgeBallOnTheTrajectory(int target) {
	// 判断轨迹上的球是否碰到会影响局面
	// 如果影响返回false
	// 如果不影响返回true
	vector<vector<int>> distribution;
	distribution = distributionAroundABall(GameState.body[target][0], GameState.body[target][1]);
	if (target % 2 == 0) {
		// 后手 其目标为先手
		if (GameState.ShotNum == 15 && !distribution[3].empty()) {
			return false;
		}
		if (firstHandInCenter.empty()) {
			return true;
		}
		else {
			for (auto j : secondHandInCenter) {
				if (j.dist_from_center < firstHandInCenter[0].dist_from_center) {
					for (auto i : distribution[3]) {
						if (j.order == i) {
							return false;
						}
					}
					for (auto i : distribution[2]) {
						if (j.order == i) {
							return false;
						}
					}
				}
			}
		}
	}
	else {
		// 先手 其目标为后手
		if (secondHandInCenter.empty()) {
			return true;
		}
		else {
			for (auto j : firstHandInCenter) {
				if (j.dist_from_center < secondHandInCenter[0].dist_from_center) {
					for (auto i : distribution[3]) {
						if (j.order == i) {
							return false;
						}
					}
					for (auto i : distribution[2]) {
						if (j.order == i) {
							return false;
						}
					}
				}
			}
		}
	}
	return true;
}

bool straightCollisionSetting(int target) {
	/* 辰龙提供参数                   */
	/* 对于 营内 目标球直线打定和打甩 */
	/* 可能打到其上我方自己的球       */
	/* 下面有球的时候返回false        */
	bool flag = false;
	vector<vector<int>> distribution;
	distribution = distributionAroundABall(GameState.body[target][0], GameState.body[target][1]);

	if (!judgeBallOnTheTrajectory(target)) {
		return false;
	}

	if (IfOccupied2(GameState.body[target][0], GameState.body[target][1]) == -1) {
		// 其下无球
		launchHighSpeedBall(GameState.body[target][0]);
		if (GameState.body[target][1] < 4.0) {
			// 球的位置比较靠后
			shot.speed = 8.0;
		}
		double x = GameState.body[target][0];
		double y = GameState.body[target][1];
		for (auto i : distribution[3]) {
			double tmpx = GameState.body[i][0];
			double tmpy = GameState.body[i][1];
			if (sqrt(pow(x - tmpx, 2) + pow(y - tmpy, 2)) < 1.1 * STONE_D) {
				// 其上方有球很贴近
				shot.speed = 5.855;
			}
		}
		flag = true;
	}
	else {
		flag = false;
	}
	return flag;
}

bool passBall(int target) {
	/*传送                     */
	/*其下多于两球返回false    */
	/*其下的球比较偏时返回false*/
	bool flag = false;
	vector<vector<int>> distribution;
	distribution = distributionAroundABall(GameState.body[target][0], GameState.body[target][1]);
	
	if (!judgeBallOnTheTrajectory(target)) {
		return false;
	}

	if (GameState.ShotNum == 4) {
		return false;
	}

	if (IfOccupied2(GameState.body[target][0], GameState.body[target][1]) == -1) {
		if (straightCollisionSetting(target)) {
			return true;
		}
	}
	else {
		// 其下有球
		if (distribution[2].size() >= 2) {
			int j = 0;
			for (auto i : distribution[2]) {
				double tmpx = GameState.body[i][0];
				double tmpy = GameState.body[i][1];
				if (distFromCenter(tmpx,tmpy) > HOUSE_R2) {
					// 目标球下面较远地方有多于两个球
					j++;
					if (j >= 2) {
						return false;
					}
				}
			}
		}
		int tmp_target = -1;
		double min_x_error;
		tmp_target = IfOccupied2(GameState.body[target][0], GameState.body[target][1]);
		min_x_error = GameState.body[tmp_target][0] - GameState.body[target][0];

		if (min_x_error < 0.15 && min_x_error > -0.18) {
			shot.speed = 6;
			shot.angle = 0;
			shot.h_x = GameState.body[tmp_target][0] - TEE_X + 0.02;
			return true;
		}
		else {
			flag = false;
		}
	}
	return flag;
}

bool rubBall(int target) {
	/*蹭球 一般不会影响其他球 向两边弹开*/
	vector<vector<int>> distribution;
	distribution = distributionAroundABall(GameState.body[target][0], GameState.body[target][1]);
	int tmp_target;
	if (!judgeBallOnTheTrajectory(target)) {
		return false;
	}
	tmp_target = IfOccupied2(GameState.body[target][0], GameState.body[target][1]);
	// 观察能不能蹭上
	if (tmp_target == -1) {
		if (straightCollisionSetting(target)) {
			return true;
		}
	}
	else {
		if (passBall(target)) {
			return true;
		}
		for (double tmpx = 0; tmpx < 0.7 * STONE_D; tmpx += 0.03) {
			if (GameState.body[tmp_target][0] > GameState.body[target][0]) {
				// 如果障碍球位于右侧 从左边看能不能找到发射位置
				distribution = distributionAroundABall(GameState.body[target][0] - tmpx, GameState.body[target][1]);
				if (distribution[2].size() == 0) {
					launchHighSpeedBall(GameState.body[target][0] - tmpx);
					shot.speed = 10.0;
					return true;
				}
			}
			else {
				// 如果障碍球位于左侧 从右边看能不能找到发射位置
				distribution = distributionAroundABall(GameState.body[target][0] + tmpx, GameState.body[target][1]);
				if (distribution[2].size() == 0) {
					launchHighSpeedBall(GameState.body[target][0] + tmpx);
					shot.speed = 10.0;
					return true;
				}
			}
		}
	}
	return false;
}

int judgeQuadrant(double x, double y) {
	/*判断球位于哪一个象限*/
	if (x > TEE_X) {
		if (y < TEE_Y) {
			return 1;
		}
		else {
			return 4;
		}
	}
	else {
		if (y < TEE_Y) {
			return 2;
		}
		else {
			return 3;
		}
	}
}

bool closeToLocation(double x, double y) {
	/*对于本垒外的位置贴近                        */
	/*贴近对手但要保证己方离本垒近且保持己方球分散*/
	/*在目标与本垒中心的连线上寻找要贴近的坐标    */

	double k, t0;
	double sinAlpha, cosAlpha;
	double xtmp, ytmp;
	int quadrant;
	quadrant = judgeQuadrant(x, y);
	if (fabs(x - TEE_X) < EPISON || fabs(y - TEE_Y) < EPISON) {
		x = x + 0.001;
		y = y + 0.001;
	}
	k = (y - TEE_Y) / (x - TEE_X);
	cosAlpha = sqrt(1 / (1 + pow(k, 2)));
	sinAlpha = sqrt(1 - pow(cosAlpha, 2));
	t0 = 0;
	if (quadrant == 1) {
		cosAlpha = -cosAlpha;
	}
	else if (quadrant == 3) {
		sinAlpha = -sinAlpha;
	}
	else if (quadrant == 4) {
		sinAlpha = -sinAlpha;
		cosAlpha = -cosAlpha;
	}
	for (;;) {
		xtmp = x + t0 * cosAlpha;
		ytmp = y + t0 * sinAlpha;
		if (shotForCurveBall(xtmp, ytmp)) {
			return true;
		}
		else {
			if (t0 > max(distFromCenter(x,y), STONE_D)) {
				shotForCenter(TEE_X, TEE_Y);
				return false;
			}
			else {
				t0 += 0.1 * STONE_R;
			}
		}
	}
}

bool straightNudge(int target) {
	/*宇烜提供参数                       */
	/*对于给定目标直线轻推               */
	/*当其下有球时返回false              */
	/*其上有球时 看情况而定              */
	/*当其不位于某个特定区域内时返回false*/
	/*轻推的最终位置 特殊处理            */
	vector<vector<int>> distribution;
	double x, y;
	x = GameState.body[target][0];
	y = GameState.body[target][1];
	distribution = distributionAroundABall(x, y);
	if (!distribution[2].empty() || 
		!(distFromCenter(x, y) <HOUSE_R + STONE_R && fabs(x - TEE_X) < HOUSE_R  - STONE_D)) {
		// 下面有球或者不位于特定区域
		return false;
	}
	else {
		double tmp_target_y;
		double v0, deltaY, deltaV;
		if (!distribution[3].empty()) {
			// 其上有球
			double y_max = 1e-6;
			for (auto i : distribution[3]) {
				if (y_max < GameState.body[i][1]) {
					y_max = GameState.body[i][1];
				}
			}
			// 其上纵坐标最大的球
			if (y_max < TEE_Y -1.8 * STONE_D) {
				tmp_target_y = TEE_Y - 1.2 * STONE_D;
			}
			else {
				// 太靠下就不推了
				return false;
			}
		}
		else {
			double tmp_loop = HOUSE_R2;
			// 推到比对方第二近的球还近
			if (target % 2 == 0) {
				// 后手
				if (firstHandInCenter.size() >= 2) {
					if (firstHandInCenter[1].dist_from_center < tmp_loop) {
						tmp_loop = firstHandInCenter[1].dist_from_center;
					}
				}
			}
			else {
				// 先手
				if (secondHandInCenter.size() >= 2) {
					if (secondHandInCenter[1].dist_from_center < tmp_loop) {
						tmp_loop = secondHandInCenter[1].dist_from_center;
					}
				}
			}
			/*if (fabs(x - TEE_X) < tmp_loop - STONE_R) {
				tmp_target_y = TEE_Y - sqrt(pow(tmp_loop, 2) - pow(x - TEE_X, 2));
				if (tmp_target_y > TEE_Y - 1.2*STONE_D) {
					tmp_target_y = TEE_Y - 1.2 * STONE_D;
				}
			}
			else {
				tmp_target_y = TEE_Y - 1.2 * STONE_D;
			}*/
			tmp_target_y = TEE_Y - 1.2 * STONE_D;
		}
		if (y < tmp_target_y) {
			deltaY = 0.1; // 贴近
		}
		else {
			deltaY = y - tmp_target_y;
		}
		v0 = speedAtFixedDistance(y);
		if (3.3602 * deltaY - 0.2779 > 0) {
			deltaV = sqrt(3.3602 * deltaY - 0.2779);
		}
		else {
			deltaV = 0.0;
		}
		shot.speed = sqrt(pow(v0,2) + pow(deltaV,2));
		shot.angle = 0;
		// shot.h_x = x - TEE_X + x_shot_error;
		shot.h_x = x - TEE_X;
		return true;
	}
}

bool spinPush(int target) {
	/* 给定坐标 发射曲线推球                     */
	/* 若上方有球阻挡 则返回false                */
	/* 可调节的参数：tmpx_err  y_dist_max overlap*/
	vector<vector<int>> distribution;
	double x_coordinate = GameState.body[target][0];
	double y_coordinate = GameState.body[target][1];
	double delta_x, delta_y;
	bool flag;
	double x_err; // 初始横坐标与目标横坐标的差值
	distribution = distributionAroundABall(x_coordinate, y_coordinate);
	if (distribution[3].size() >= 2) {
		// 上方有两个球的时候
		// 场内有3个球就推不动 返回false
		int j = 0;
		for (auto i : distribution[3]) {
			double tmpx = GameState.body[i][0];
			double tmpy = GameState.body[i][1];
			if (sqrt(pow(tmpx - x_coordinate, 2) + pow(tmpy - y_coordinate, 2)) < HOUSE_R1 + STONE_R) {
				j++;
				if (j >= 2) {
					return false;
				}
			}
		}
	}
	if (distribution[2].size() == 0) {
		return straightNudge(target);
	}
	else {
		// 如果球的正下方有球
		// 判断离y_coordinate最近的球的距离 是否足够远
		double y_dist;
		int j = -1;
		double tmpx_err = 1.3;
		double y_dist_max = 4.2;
		// 获取下方与它最近的球的距离
		if (IfOccupied(x_coordinate, y_coordinate) == -1) {
			y_dist = 1e6;
		}
		else {
			j = IfOccupied(x_coordinate, y_coordinate);
			y_dist = GameState.body[j][1] - y_coordinate;
		}

		if (j == -1) {
			if (rand() % 2 == 0) {
				flag = true;
			}
			else {
				flag = false;
			}
		}
		else {
			if (fabs(GameState.body[j][0] - x_coordinate) < 0.03 &&
				GameState.body[j][1] - y_coordinate < 4.4) {
				return false;
			}
			else {
				if (GameState.body[j][0] - x_coordinate < 0) {
					// true从右边打
					flag = true;
				}
				else {
					//false 从左边打 
					flag = false;
				}
			}
		}

		if (y_dist < y_dist_max) {
			// 如果不是足够远，则不能旋到准确位置，有一定偏差
			// 确定重合的系数overlap
			double overlap = -0.36 * y_dist + 1.522;
			if (overlap > 1) {
				// 下方的球靠的很近 肯定碰上
				return false;
			}
			if (flag == false) {
				// 从左边打
				x_coordinate = x_coordinate - overlap * STONE_R;
				if (x_coordinate - tmpx_err > PLAYAREA_X_MIN) {
					shot.h_x = x_coordinate - tmpx_err;
				}
				else {
					shot.h_x = PLAYAREA_X_MIN;
					return false;
				}
				double k = -1.0175 * y_coordinate + 20.546;
				double b = -0.5891;
				// 注意-k
				if (!trajectoryJudgment(-k, b, x_coordinate, y_coordinate)) {
					return false;
				}
			}
			else {
				// 从右边打
				x_coordinate = x_coordinate + overlap * STONE_R;
				if (x_coordinate + tmpx_err < PLAYAREA_X_MAX) {
					shot.h_x = x_coordinate + tmpx_err;
				}
				else {
					shot.h_x = PLAYAREA_X_MAX;
					return false;
				}
				double k = -0.699 * y_coordinate + 19.552;
				double b = -0.56183;
				if (!trajectoryJudgment(k, b, x_coordinate, y_coordinate)) {
					return false;
				}
			}
		}
		else {
			// 如果足够远
			bool tmp_flag =true;

			// 从左边打向右旋x_err为负，w为正
			if (x_coordinate - tmpx_err > PLAYAREA_X_MIN) {
				shot.h_x = x_coordinate - tmpx_err;
			}
			else {
				shot.h_x = PLAYAREA_X_MIN;
				tmp_flag = false;
			}
			double k = -1.0175 * y_coordinate + 20.546;
			double b = -0.5891;
			// 注意-k
			if (!trajectoryJudgment(-k, b, x_coordinate, y_coordinate)) {
				tmp_flag = false;
			}

			if(tmp_flag == false) {
				// 从右边打向左旋x_err为正, w为负
				if (x_coordinate + tmpx_err < PLAYAREA_X_MAX) {
					shot.h_x = x_coordinate + tmpx_err;
				}
				else {
					shot.h_x = PLAYAREA_X_MAX;
					return false;
				}
				double k = -0.699 * y_coordinate + 19.552;
				double b = -0.56183;
				if (!trajectoryJudgment(k, b, x_coordinate, y_coordinate)) {
					return false;
				}
			}
		}
		if (y_dist < 3.62) {
			delta_x = 0.27;
			delta_y = -2.26;
			if (GameState.body[target][1] > TEE_Y + 0.507 - 1.5 * STONE_R) {
				// 防止推完目标球更近
				return false;
			}
		}
		else if (y_dist < 4.12) {
			delta_x = 0.15;
			delta_y = -1.5;
			if (GameState.body[target][1] > TEE_Y + 0.363 - 1.5 * STONE_R) {
				return false;
			}
		}
		else if(y_dist < 4.45){
			delta_x = 0.1;
			delta_y = -1.3;
			if (GameState.body[target][1] > TEE_Y + 0.331 - 1.5 * STONE_R) {
				return false;
			}
		}
		else {
			delta_x = 0.13;
			delta_y = -1.4;
			if (GameState.body[target][1] > TEE_Y + 0.331 - 1.5 * STONE_R) {
				return false;
			}
		}
		if (flag == true) {
			delta_x = -delta_x;
		}
		x_err = shot.h_x - x_coordinate - delta_x;
		shot.h_x = shot.h_x - TEE_X;
		y_coordinate = y_coordinate + delta_y;
	}

	if (solveForVW(y_coordinate, x_err)) {
		return true;
	}
	else {
		// 若没有找到w的解(修改参数后一般有解)
		return false;
	}
}

/*void spinStrike(int target) {
	double x, y;
	vector<vector<int>> distribution;
	bool return_flag = false;
	x = GameState.body[target][0];
	y = GameState.body[target][1];
	distribution = distributionAroundABall(x, y);

	int flag;
	int tmp_target = -1;
	double y_dist = 1e6;
	for (auto i : distribution[2]) {
		if (GameState.body[i][1] - y < y_dist &&
			GameState.body[i][1] - y > 1.5 * STONE_D) {
			tmp_target = i;
			y_dist = GameState.body[i][1] - y;
		}
	}
	y_dist = GameState.body[tmp_target][1];

	if (x > TEE_X) {
		// 从右边打
		flag = 1;
	}
	else {
		// 从左边打
		flag = -1;
	}

	// 旋打
	if (y_dist > 8.2) {
		shot.angle = -flag * 10.0;
		shot.speed = 6;
		shot.h_x = flag * 1.2 + x - TEE_X;
	}
	else if (y_dist > 7.0) {
		shot.angle = -flag * 10;
		shot.speed = 5.5;
		shot.h_x = flag * 1.33 + x - TEE_X;
	}
}*/

void spinStrike(int target) {
	double x, y;
	vector<vector<int>> distribution;
	x = GameState.body[target][0];
	y = GameState.body[target][1];
	distribution = distributionAroundABall(x, y);
	int flag;
	int tmp_target = 0;
	double y_dist = 1e6;
	for (auto i : distribution[2]) {
		if (GameState.body[i][1] - y < y_dist &&
			GameState.body[i][1] - y > HOUSE_R1) {
			tmp_target = i;
			y_dist = GameState.body[i][1] - y;
		}
	}
	if (GameState.body[tmp_target][0] < x) {
		// 从右边打
		flag = 1;
	}
	else {
		// 从左边打
		flag = -1;
	}

	if (rand() % 3 != 0) {
		shot.angle = -flag * 9;
		shot.speed = 5.5;
		shot.h_x = flag * 1.08 + x - TEE_X;
	}
	else {
		shot.angle = -flag * 10;
		shot.speed = 5.5;
		shot.h_x = flag * 1.33 + x - TEE_X;
	}

}

void shotForCenter(double x, double y) {
	/*旋定中央附近*/
	double  t0;
	for (t0 = 0;; t0 += 0.01) {
		if (shotForCurveBall(x - t0 * cos(PI / 4), y + t0 * sin(PI / 4))
			|| shotForCurveBall(x + t0 * cos(PI / 4), y + t0 * sin(PI / 4))
			|| shotForCurveBall(x - t0 * cos(PI / 6), y + t0 * sin(PI / 6))
			|| shotForCurveBall(x + t0 * cos(PI / 6), y + t0 * sin(PI / 6))
			|| shotForCurveBall(x - t0 * cos(PI / 3), y + t0 * sin(PI / 3))
			|| shotForCurveBall(x + t0 * cos(PI / 3), y + t0 * sin(PI / 3))
			) {
			break;
		}
	}
}

bool scanPlaceholder(int label) {
	/*扫描下方是否有可以利用的占位壶*/
	/*label 0 先手 label 1 后手     */
	bool flag = false;
	double t = 0.0;
	double dist;     // 对手目前的距离
	if (label == 0) {
		// 先手
		if (!secondHandInCenter.empty()) {
			dist = secondHandInCenter[0].dist_from_center;
		}
		else {
			dist = 1e6;
		}
	}
	else {
		// 后手
		if (!firstHandInCenter.empty()) {
			dist = firstHandInCenter[0].dist_from_center;
		}
		else {
			dist = 1e6;
		}
	}
	for ( ; t < HOUSE_R; t = t + STONE_R) {
		double x ;
		x = TEE_X + t;
		if (IfOccupied2(x, TEE_Y) == -1) {
			x = TEE_X - t;
			if (IfOccupied2(x, TEE_Y) == -1) {
				continue;
			}
			else {
				int target = IfOccupied2(x, TEE_Y);
				for (double t2 = 0.0; t2 < HOUSE_R; t2 = t2 + 0.1) {
					if (shotForCurveBall(GameState.body[target][0], TEE_Y - t2)) {
						if (distFromCenter(GameState.body[target][0], TEE_Y - t2) < dist) {
							return true;
						}
						else {
							break;
						}
					}
					else {
						continue;
					}
				}
			}
		}
		else {
			int target = IfOccupied2(x, TEE_Y);
			for (double t2 = 0.0; t2 < HOUSE_R; t2 = t2 + 0.1) {
				if (shotForCurveBall(GameState.body[target][0], TEE_Y - t2)) {
					if (distFromCenter(GameState.body[target][0], TEE_Y - t2) < dist) {
						return true;
					}
					else {
						break;
					}
				}
				else {
					continue;
				}
			}
		}
	}
	return false;
}

bool addPlaceholder(double x, double y) {
	// 位于中心才加两个保护球
	// 下方3个球的时候返回false
	// 下方两个球的时候有一定距离的时候返回false
	// 添加成功返回true
	vector<vector<int>> distribution;
	distribution = distributionAroundABall(x, y);
 	if (distFromCenter(x, y) < center_dist) {
		if (IfOccupied2(x, y) == -1) {
			if (y + protect_dist > 10.2) {
				closeToLocation(x, 9.5);
			}
			else {
				closeToLocation(x, y + protect_dist);
			}
			return true;
		}
		else {
			if (distribution[2].size() == 1) {
				closeToLocation(x + 0.25 * STONE_R, TEE_Y + 0.9 * protect_dist);
			}
			else {
				int k = 0;
				vector<int> tmpdistribution;
				for (auto i : distribution[2]) {
					if (distFromCenter(GameState.body[i][0], GameState.body[i][1]) > HOUSE_R + STONE_D) {
						// 位于自由保护区
						tmpdistribution.push_back(i);
						k++;
					}
				}
				if (k >= 3) {
					return false;
				}
				else {
					if (k == 2) {
						double x1 = GameState.body[tmpdistribution[0]][0];
						double y1 = GameState.body[tmpdistribution[0]][1];
						double x2 = GameState.body[tmpdistribution[1]][0];
						double y2 = GameState.body[tmpdistribution[1]][1];
						if (sqrt(pow(x1 - x2, 2) + pow(y1 - y2, 2)) < 1.1 * STONE_D) {
							closeToLocation(x + 0.25 * STONE_R, TEE_Y + 0.9 * protect_dist);
						}
						else {
							return false;
						}
					}
					else {
						closeToLocation(x + 0.25 * STONE_R, TEE_Y + 0.9 * protect_dist);
					}
				}
			}
		}
	}
	else {
		return false;
	}
}

bool addPlaceholder2(double x, double y) {
	// 向侧面加保护球
	// 下方有保护球的时候加侧面保护球
	// 添加成功返回true
	vector<vector<int>> distribution;
	distribution = distributionAroundABall(x, y);

	double y_dist = 1e6;
	int j = 0;
	// 获取下方与它最近的球的距离
	if (IfOccupied2(x,y) != -1) {
		for (auto i : distribution[2]) {
			if (GameState.body[i][1] - y < y_dist &&
				GameState.body[i][1] - y > 1.5 * STONE_D) {
				j = i;
				y_dist = GameState.body[i][1] - y;
			}
		}
		if (GameState.body[j][0] > TEE_X) {
			// 靠右 左边加
			distribution = distributionAroundABall(2.2, TEE_Y);
			for (auto i : distribution[2]) {
				double tmpx = GameState.body[i][0];
				double tmpy = GameState.body[i][1];
				if (sqrt(pow(tmpx - 2.2, 2) + pow(tmpy - 6.8, 2)) < 0.5 * STONE_R) {
					return false;
				}
			}
			if (closeToLocation(2.2, 6.8)) {
				return true;
			}
			else {
				shotForCenter(TEE_X, TEE_Y);
				return false;
			}
		}
		else {
			distribution = distributionAroundABall(2.55, TEE_Y);
			for (auto i : distribution[2]) {
				double tmpx = GameState.body[i][0];
				double tmpy = GameState.body[i][1];
				if (sqrt(pow(tmpx - 2.55, 2) + pow(tmpy - 6.8, 2)) < 0.5 * STONE_R) {
					return false;
				}
			}
			if (closeToLocation(2.55, 6.8)) {
				return true;
			}
			else {
				shotForCenter(TEE_X, TEE_Y);
				return false;
			}
		}
	}
	else {
		return false;
	}
}

bool lastBallForSecondHand() {
	// 发出来球返会true
	if (!firstHandInCenter.empty()) {
		double r = firstHandInCenter[0].dist_from_center;
		r = r - STONE_R;
		for (; r < HOUSE_R; r += STONE_R) {
			for (int i = 0; i < 1000; ) {
				double t1 = rand() % int(2 * r * 100);
				double t2 = rand() % int(2 * r * 100);
				double	x = t1 / 100.0 + TEE_X - r;
				double	y = t2 / 100.0 + TEE_Y - r;
				if (distFromCenter(x, y) < r) {
					if (shotForCurveBall(x, y)) {
						return true;
					}
					else {
						i++;
					}
				}
			}
		}
		return false;
	}
	else {
		return false;
	}
}

bool handleBall(int target) {
	/*对于一个目标的处理*/
	/*直线轻推          */
	/*若否直线打甩或打定*/
	/*若否球有保护球，返回false*/
	bool flag = false;
	if (!straightNudge(target)) {
		if (straightCollisionSetting(target)) {
			flag = true;
		}
	}
	else {
		flag = true;
	}
	return flag;
}

bool handleBall2(int target) {
	bool flag = false;
	if (!straightNudge(target)) {
		if (rand() % 2 == 0) {
			if (!passBall(target)) {
				if (!spinPush(target)) {
					flag = false;
				}
				else {
					flag = true;
				}
			}
			else {
				flag = true;
			}
		}
		else {
			if (!spinPush(target)) {
				if (!passBall(target)) {
					flag = false;
				}
				else {
					flag = true;
				}
			}
			else {
				flag = true;
			}
		}
	}
	else {
		flag = true;
	}
	return flag;
}

void handleBallInCenter(int target) {
	/*对于中心的球               */
	/*首选直线推 否则直线打定    */
	/*若否旋推、传送随机 其次旋打*/
	if (!handleBall(target)) {
		// 其下有球
		if (rand() % 2 == 0) {
			if (!spinPush(target)) {
				if (!passBall(target)) {
					spinStrike(target);
				}
			}
		}
		else {
			if (!passBall(target)) {
				if (!spinPush(target)) {
					spinStrike(target);
				}
			}
		}
	}
}

void getBestShot() {
	int score;
	int curScore; // 此时本垒中的分数
	sortDist();
	ballInCenterFunc();
	score = sumScore();
	curScore = currentScoreInCenter();

	//int flag;
	//cout << "是否手动发射(1:手动)：";
	//cin >> flag;
	//if (flag == 1) {
	//	cout << "shot.speed:";
	//	cin >> shot.speed;
	//	cout << "shot.angle:";
	//	cin >> shot.angle;
	//	cout << "shot.h_x:";
	//	cin >> shot.h_x;
	//	return;
	//}

	randBall();  // 防止下面的函数没有找到任何一种情况

	if (GameState.ShotNum % 2 == 0) {
		// 先手
		if (GameState.ShotNum == 0) {
			// 第一球 尽可能靠近本垒  不进入
			if (score >= 2) {
				shotForStraightBall(TEE_X, 10.1);
			}
			else {
				shotForStraightBall(TEE_X, TEE_Y + HOUSE_R + 2.5 * STONE_R);
			}
		}
		if (GameState.ShotNum == 2) {
			if (!secondHandInCenter.empty() && secondHandInCenter[0].dist_from_center < HOUSE_R + STONE_R) {
				//如果对方的壶进营
				if (!handleBall(secondHandInCenter[0].order)) {
					if (!spinPush(secondHandInCenter[0].order)) {
						shotForCenter(TEE_X, TEE_Y);
					}
				}
			}
			else {
				if (distFromCenter(GameState.body[0][0], GameState.body[0][1]) < center_dist) {
					closeToLocation(TEE_X, TEE_Y + protect_dist);
				}
				else {
					shotForCenter(TEE_X, TEE_Y);
				}
			}
		}
		if (GameState.ShotNum >= 4 && GameState.ShotNum <= 12) {
			if (curScore <= -1) {
				int tmp_target = secondHandInCenter[0].order;
				if (!handleBall2(tmp_target)) {
					if (  (score >= 0 && !rubBall(tmp_target)) || score < 0) {
						//当前总体处于劣势
						if (numberOfADistance(center_dist) == 0) {
							// 但对方没有占领中心
							if (IfOccupied2(TEE_X, TEE_Y) == -1) {
								// 当前中线无占位壶 对方也没有占领中心
								// 扫描下方是否有占位壶可以使用
								if (!scanPlaceholder(0)) {
									closeToLocation(TEE_X, TEE_Y + protect_dist);
								}
							}
							else {
								// 当前有占位壶
								// 把球放在占位壶后
								shotForCenter(GameState.body[IfOccupied2(TEE_X, TEE_Y)][0], TEE_Y);
							}
						}
						else {
							// 对方占领中心
							handleBallInCenter(tmp_target);
						}
					}
				}
			}
			if (curScore == 0) {
				if (secondHandInCenter.empty()) {
					if (firstHandInCenter.empty()) {
						//营内无球，直接进营
						if (IfOccupied2(TEE_X, TEE_Y) == -1) {
							if (!scanPlaceholder(0)) {
								closeToLocation(TEE_X, TEE_Y + protect_dist);
							}
						}
						else {
							shotForCenter(GameState.body[IfOccupied2(TEE_X, TEE_Y)][0], TEE_Y);
						}
					}
				}
				else {
					// 概率很小 双方的球到中心的距离一样
					if (numberOfADistance(center_dist) == 0) {
						shotForCenter(TEE_X, TEE_Y);
					}
					else {
						handleBallInCenter(secondHandInCenter[0].order);
					}
				}
			}
			if (curScore >= 1) {
				int score_ball = firstHandInCenter[0].order;
				if (IfOccupied2(GameState.body[score_ball][0], GameState.body[score_ball][1]) == -1) {
					if (GameState.body[score_ball][1] + protect_dist > 10.2) {
						closeToLocation(GameState.body[score_ball][0], 9.5);
					}
					else {
						closeToLocation(GameState.body[score_ball][0], GameState.body[score_ball][1] + protect_dist);
					}
				}
				else {
					if (secondHandInCenter.empty() || (!secondHandInCenter.empty() && !handleBall2(secondHandInCenter[0].order))) {
						if ((!secondHandInCenter.empty() && !rubBall(secondHandInCenter[0].order))) {
							// 有保护球
							if (IfOccupied2(TEE_X, TEE_Y) == -1) {
								closeToLocation(TEE_X, TEE_Y + protect_dist);
							}
							else {
								if (numberOfADistance(center_dist2) == 0) {
									shotForCenter(GameState.body[IfOccupied2(TEE_X, TEE_Y)][0], TEE_Y);
								}
								else {
									if (!addPlaceholder(firstHandInCenter[0].x_coordinate, firstHandInCenter[0].y_coordinate)) {
										// 已经有两个保护求了
										if (!addPlaceholder2(firstHandInCenter[0].x_coordinate, firstHandInCenter[0].y_coordinate)) {
											randBall();
											for (auto i : secondHandInCenter) {
												if (handleBall(i.order)) {
													break;
												}
											}
										}
									}
								}
							}
						}
					}
				}
			}
		}
		if (GameState.ShotNum == 14) {
			if (curScore < 0) {
				int target = secondHandInCenter[0].order;
				if (!straightNudge(target)) {
					// 其下有球
					if (!spinPush(target)) {
						if (IfOccupied(TEE_X, TEE_Y) == -1) {
							if (!closeToLocation(GameState.body[target][0], GameState.body[target][1])) {
								if (!scanPlaceholder(0)) {
									shotForCenter(TEE_X, TEE_Y);
								}
							}
						}
						else {
							shotForCenter(TEE_X, TEE_Y);
						}
					}
				}
			}
			else {
				if (curScore == 0) {
					if (!scanPlaceholder(0)) {
						shotForCenter(TEE_X, TEE_Y+ STONE_D);
					}
				}
				else {
					if (firstHandInCenter[0].dist_from_center < center_dist) {
						if (IfOccupied2(TEE_X, TEE_Y) == -1) {
							closeToLocation(TEE_X, TEE_Y + protect_dist);
						}
						else {
							if (numberOfADistance(center_dist2) == 0) {
								shotForCenter(GameState.body[IfOccupied2(TEE_X, TEE_Y)][0], TEE_Y);
							}
							else {
								if (!addPlaceholder(firstHandInCenter[0].x_coordinate, firstHandInCenter[0].y_coordinate)) {
									// 已经有两个保护求了
									if (!addPlaceholder2(firstHandInCenter[0].x_coordinate, firstHandInCenter[0].y_coordinate)) {
										randBall();
									}
								}
							}
						}
					}
					else {
						shotForCenter(TEE_X, TEE_Y + STONE_D);
					}
				}
			}
		}
	}
	else {
		// 后手
		curScore = -curScore; // 先手与后手的分数互为相反数
		if (GameState.ShotNum == 1) {
			// 对方的第一个壶是否进营
			if (distFromCenter(GameState.body[0][0], GameState.body[0][1]) < HOUSE_R + STONE_R) {
				// 如果进营推
				if (!handleBall(0)) {
					shotForCenter(TEE_X, TEE_Y);
				}
			}
			else {
				// 否则先手应该中区占位 后手旋进本垒
				shotForCenter(TEE_X, TEE_Y);
			}
		}
		if (GameState.ShotNum == 3) {
			if (!firstHandInCenter.empty() && firstHandInCenter[0].dist_from_center < HOUSE_R + STONE_R) {
				if (!handleBall(firstHandInCenter[0].order)) {
					if (!spinPush(firstHandInCenter[0].order)) {
						shotForCenter(TEE_X, TEE_Y);
					}
				}
			}
			else if (distFromCenter(GameState.body[1][0], GameState.body[1][1]) < center_dist) {
				//自己的壶在营内,放在其上面以保护
				if (!closeToLocation(GameState.body[1][0], TEE_Y + protect_dist)) {
					shotForCenter(TEE_X, TEE_Y);
				}
			}
			else {
				shotForCenter(TEE_X, TEE_Y);
			}
		}
		if (GameState.ShotNum >= 5 && GameState.ShotNum <= 13) {
			if (curScore <= -1) {
				int tmp_target = firstHandInCenter[0].order;
				if (!handleBall2(tmp_target)) {
					if ((score >= 0 && !rubBall(tmp_target)) || score < 0) {
						//当前总体处于劣势
						if (numberOfADistance(center_dist) == 0) {
							// 但对方没有占领中心
							if (IfOccupied2(TEE_X, TEE_Y) == -1) {
								// 当前中线无占位壶 对方也没有占领中心
								// 扫描下方是否有占位壶可以使用
								if (!scanPlaceholder(1)) {
									closeToLocation(TEE_X, TEE_Y + protect_dist);
								}
							}
							else {
								// 当前有占位壶
								// 把球放在占位壶后
								shotForCenter(GameState.body[IfOccupied2(TEE_X, TEE_Y)][0], TEE_Y);
							}
						}
						else {
							// 对方占领中心
							handleBallInCenter(tmp_target);
						}
					}
				}
			}
			if (curScore == 0) {
				if (firstHandInCenter.empty()) {
					if (secondHandInCenter.empty()) {
						//营内无球，直接进营
						if (IfOccupied2(TEE_X, TEE_Y) == -1) {
							if (!scanPlaceholder(1)) {
								closeToLocation(TEE_X, TEE_Y + protect_dist);
							}
						}
						else {
							shotForCenter(GameState.body[IfOccupied2(TEE_X, TEE_Y)][0], TEE_Y);
						}
					}
				}
				else {
					// 概率很小 双方的球到中心的距离一样
					if (numberOfADistance(center_dist) == 0) {
						shotForCenter(TEE_X, TEE_Y);
					}
					else {
						handleBallInCenter(firstHandInCenter[0].order);
					}
				}
			}
			if (curScore >= 1) {
				int score_ball = secondHandInCenter[0].order;
				if (IfOccupied2(GameState.body[score_ball][0], GameState.body[score_ball][1]) == -1) {
					if (GameState.body[score_ball][1] + protect_dist > 10.2) {
						closeToLocation(GameState.body[score_ball][0], 9.5);
					}
					else {
						closeToLocation(GameState.body[score_ball][0], GameState.body[score_ball][1] + protect_dist);
					}
				}
				else {
					if (firstHandInCenter.empty() || (!firstHandInCenter.empty() && !handleBall2(firstHandInCenter[0].order))) {
						if ((!firstHandInCenter.empty() && !rubBall(firstHandInCenter[0].order))) {
							// 有保护球
							if (IfOccupied2(TEE_X, TEE_Y) == -1) {
								closeToLocation(TEE_X, TEE_Y + protect_dist);
							}
							else {
								if (numberOfADistance(center_dist2) != 0) {
									if (!addPlaceholder(secondHandInCenter[0].x_coordinate, secondHandInCenter[0].y_coordinate)) {
										// 已经有两个保护球了
										if (!addPlaceholder2(secondHandInCenter[0].x_coordinate, secondHandInCenter[0].y_coordinate)) {
											randBall();
											for (auto i : firstHandInCenter) {
												if (handleBall(i.order)) {
													break;
												}
											}
										}
									}
								}
								else {
									shotForCenter(GameState.body[IfOccupied2(TEE_X, TEE_Y)][0], TEE_Y);
								}
							}
						}
					}
				}
			}
		}
		if (GameState.ShotNum == 15) {
			if (curScore <= -1) {
				if (curScore == -1) {
					// 说明只有他的一个球是位于最近的
					// 再往外一个球就是后手的了
					if (straightCollisionSetting(firstHandInCenter[0].order)) {
						return;
					}
					else {
						if (spinPush(firstHandInCenter[0].order)) {
							/*shot.speed += 1.0;
							shot.angle += 0.5;
							if (shot.h_x + TEE_X > GameState.body[firstHandInCenter[0].order][0]) {
								shot.h_x -= 0.15;
							}
							else {
								shot.h_x += 0.15;
							}*/
							return;
						}
					}
				}
				
				int target = firstHandInCenter[0].order;
				if (!handleBall(target)) {
					// 其下有球
					if (!spinPush(target)) {
						if (!secondHandInCenter.empty() && secondHandInCenter[0].dist_from_center > center_dist) {
							if (!passBall(firstHandInCenter[0].order)) {
								lastBallForSecondHand();
							}
						}
						else {
							lastBallForSecondHand();
						}
					}
				}

			}
			if (curScore == 0) {
				// 很大程度上场内无球
				shotForCenter(TEE_X, TEE_Y);
			}
			if (curScore >= 1) {
				if (firstHandInCenter.size() == 0) {
					randBall();
				}
				else {
					if (firstHandInCenter.size() == 1) {
						int tmp_target = firstHandInCenter[0].order;
						if (IfOccupied2(GameState.body[tmp_target][0], GameState.body[tmp_target][1]) != -1 ||
							!straightCollisionSetting(firstHandInCenter[0].order)) {
							if (!lastBallForSecondHand()) {
								shotForCenter(TEE_X, TEE_Y);
							}
						}
					}
					else {
						if (!lastBallForSecondHand()) {
							shotForCenter(TEE_X, TEE_Y);
						}
					}
				}
			}
		}
	}
}

void getSweep() {
	sweepDistance = 0;
}

//! initialize GAMESTATE
void initGameState(GAMESTATE *pgs) {
	memset(pgs->body, 0x00, sizeof(float) * 32);
	memset(pgs->Score, 0x00, sizeof(int) * 10);
	pgs->LastEnd = 0;
	pgs->CurEnd = 0;
	pgs->ShotNum = 0;
	pgs->WhiteToMove = 0;
}

//! send command
void sendCommand(const char *message)
{
	 Sleep(500);
	int send_len = send(m_server, message, strlen(message), 0);
	if (send_len < 0) {
		std::cout << "send failed" << std::endl;
	}
	std::cout << "send " + string(message) << std::endl;
}

//! delete newline
void DeleteNL(char* Message)
{
	char* p;
	p = Message;
	// 0x00就是0，就是NULL，就是‘\0’
	while (*p != 0x00) {
		if (*p == '\n' || *p == '\r') {
			*p = 0x00;
			break;
		}
		p++;
	}
	return;
}

//! receive command
void recvCommand(char *message, SOCKET s_server, size_t size)
{
	// 返回接受数据的字节数，没连接服务器返回-1
	int length = recv(s_server, message, size, 0);
	// 删除命令的回车换行符
	DeleteNL(message);
	if (length < 0) {
		std::cout << "recv failed" << std::endl;
	}
	else if(length > 0) {
		// 模拟比赛时，可以再模拟比赛的命令行中看到接受的信息
		std::cout << "recv message:" << message << std::endl;
	}
}

//! get argument from command
bool GetArgument(char *lpResult, size_t numberOfElements, char *Message, int n)
{
	char *p, *q;

	if (Message != NULL) {
		p = Message;
		while (*p == ' ') {
			p++;
		}

		// 提取参数，比如POSITION、SATSTATE等命令
		for (int i = 0; i<n; i++) {
			// 跳过前面空格隔开的n个元素
			while (*p != ' ') {
				if (*p == 0x00) {
					return false;
				}
				p++;
			}
			while (*p == ' ') {
				p++;
			}
		}

		q = strstr(p, " "); // 获得数据的结束位置
		if (q == NULL) {
			// 处理一条命令的最后一个元素，结束位置为NULL
			strcpy(lpResult, p);
		}
		else {
			 strncpy_s(lpResult, numberOfElements, p, q - p);
			if ((unsigned int)(q - p) < numberOfElements) {
				lpResult[q - p] = 0x00;
			}
		}
	}

	return true;
}

//! process command
bool processCommand(char *command)
{
	// 不同类型的命令对应说明书中的消息解释
	char cmd[BUFSIZE];
	char buffer[BUFSIZE];
	// delete newline
	DeleteNL(command);

	// get command
	if (!GetArgument(cmd, sizeof(cmd), command, 0)) {
		return false;
	}

	// process command
	if (_stricmp(cmd, "NEWGAME") == 0) {
	}
	else if (_stricmp(cmd, "ISREADY") == 0) {
		// initialize GameState
		initGameState(&GameState);
		sendCommand("READYOK");
		sendCommand("NAME 厦天要加冰");
	}
	else if (_stricmp(cmd, "POSITION") == 0) {
		for (int i = 0; i < 16; i++) {
			// get x cordinate
			if (!GetArgument(buffer, sizeof(buffer), command, 2 * i + 1)) {
				return false;
			}
			GameState.body[i][0] = (float)atof(buffer);

			// get y cordinate
			if (!GetArgument(buffer, sizeof(buffer), command, 2 * i + 2)) {
				return false;
			}
			GameState.body[i][1] = (float)atof(buffer);
		}
	}
	else if (_stricmp(cmd, "SETSTATE") == 0) {
		// number of current shots
		if (GetArgument(buffer, sizeof(buffer), command, 1) == FALSE) {
			return false;
		}
		GameState.ShotNum = atoi(buffer);

		// number of current ends
		if (GetArgument(buffer, sizeof(buffer), command, 2) == FALSE) {
			return false;
		}
		GameState.CurEnd = atoi(buffer);

		// number of last
		if (GetArgument(buffer, sizeof(buffer), command, 3) == FALSE) {
			return false;
		}
		GameState.LastEnd = atoi(buffer);
		if (GetArgument(buffer, sizeof(buffer), command, 4) == FALSE) {
			return false;
		}
		if (atoi(buffer) == 1) {
			GameState.WhiteToMove = true;
		}
		else {
			GameState.WhiteToMove = false;
		}
	}
	else if (_stricmp(cmd, "GO") == 0) {
		// SHOTINFO shot(0.0f, 0.0f, 0.0f);

		// get a Shot by getBestShot (defined in 'strategy.cpp')
		getBestShot();

		// create BESTSHOT command
		sprintf_s(buffer, sizeof(char) * BUFSIZE, "BESTSHOT %f %f %f", shot.speed, shot.h_x, shot.angle);

		// send BESTSHOT command
		sendCommand(buffer);
	}
	else if (_stricmp(cmd, "SCORE") == 0) {
		// get score of previous end
		if (GetArgument(buffer, sizeof(buffer), command, 1) == FALSE) {
			return false;
		}
		GameState.Score[GameState.CurEnd] = atoi(buffer);
	}
	else if (_stricmp(cmd, "MOTIONINFO") == 0)
	{
		// MOTIONINFO motionInfo;
		if (GetArgument(buffer, sizeof(buffer), command, 1) == FALSE) {
			return false;
		}
		motionInfo.x_coordinate = (float)atof(buffer);

		if (GetArgument(buffer, sizeof(buffer), command, 2) == FALSE) {
			return false;
		}
		motionInfo.y_coordinate = (float)atof(buffer);

		if (GetArgument(buffer, sizeof(buffer), command, 3) == FALSE) {
			return false;
		}
		motionInfo.x_velocity = (float)atof(buffer);

		if (GetArgument(buffer, sizeof(buffer), command, 4) == FALSE) {
			return false;
		}
		motionInfo.y_velocity = (float)atof(buffer);

		if (GetArgument(buffer, sizeof(buffer), command, 5) == FALSE) {
			return false;
		}
		motionInfo.angular_velocity = (float)atof(buffer);

		// create SWEEP command
		getSweep(); // you need to estimate the distance you want to sweep
		sprintf_s(buffer, sizeof(char) * BUFSIZE, "SWEEP %f", sweepDistance);
		// send SWEEP command
		sendCommand(buffer);
	}

	return true;
}

void initialization() {
	// initialize socket
	WORD w_req = MAKEWORD(2, 2);//version
	WSADATA wsadata;
	int err;
	err = WSAStartup(w_req, &wsadata);
	if (err != 0) {
		std::cout << "initialization failed" << std::endl;
	}
	else {
		std::cout << "initialization succeed" << std::endl;
	}
	// check socket version
	if (LOBYTE(wsadata.wVersion) != 2 || HIBYTE(wsadata.wHighVersion) != 2) {
		std::cout << "socket version is not correct" << std::endl;
		WSACleanup();
	}
	else {
		std::cout << "socket version is correct" << std::endl;
	}
}

int main()
{
	// TCP IP 套接字通信
	SOCKET s_server;
	SOCKADDR_IN server_addr;
	initialization();
	// 绑定套接字到一个IP地址和一个端口上
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.S_un.S_addr = inet_addr(IP); // 127.0.0.1
	server_addr.sin_port = htons(PORT); // 7788
	s_server = socket(AF_INET, SOCK_STREAM, 0); // 创建套接字
	// 接受连接请求
	if (connect(s_server, (SOCKADDR*)&server_addr, sizeof(SOCKADDR)) == SOCKET_ERROR) {
		std::cout << "Server connection failed" << std::endl;
		WSACleanup();
		// return 0;
	}
	else {
		std::cout << "Server connection success" << std::endl;
	}
	m_server = s_server;
	char message[BUFSIZE]; // 1024

	// process command
	while (1) {
		memset(message, 0, sizeof(message));
		recvCommand(message, s_server, sizeof(message));
		// processing multiple instructions in one message buffer
		for (int i = 0, j = 0; i + j < sizeof(message); j++) {
			if (message[i + j] == 0x00) {
				processCommand(&message[i]);
				// i可以看作命令的起始地址，处理完一条命令之后，更新i，将j置0
				i = i + j + 1;
				j = 0;
			}
		}
	}

	return 0;
}

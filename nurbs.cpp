#include <GL/glut.h>
#include <iostream>
#include <fstream>
#include <iomanip>
#include <cstring>
#include <cstdlib>
#include <unistd.h>

#if WIN32
static void *font=((void*)6);
#else
static void *font=GLUT_BITMAP_HELVETICA_12;
#endif

#define X 0
#define Y 1
#define Z 2
#define W 3
#define MAX_DETAIL 499
#define MARGIN 15
#define ARRAY_MAX 1000
#define SEL_NONE -1
#define SEL_NEW -2
#define ZOOM_FACTOR 1.1

using namespace std;
static float // colores
	color_fondo[]={0.95f,0.98f,1.0},       // color de fondo
	color_cline[]={.2f,.2f,.4f}, // poligono de control
	color_cpoint[]={.3f,0.2f,0.8f}, // puntos de control
	color_nurb[]={.8f,.6f,.6f}, // curva
	color_knot[]={.4f,0.f,0.f}, // line de knots
	color_texto[]={.3f,0.2f,0.8f}, // eje
	color_detail[]={.6f,.8f,.6f}, // lineas accesorias
	color_new[]={.8f,.6f,.6f}; // nuevo nodo


enum {
	MT_NONE,
	MT_CONTROL,
	MT_COORD_W,
	MT_KNOT,
	MT_DETAIL,
	MT_MOVE
};

int w=640,h=480;
bool minimized=false;

float sel_x,sel_y;
int sel_control=SEL_NONE, sel_knot=SEL_NONE;
bool sel_detail;
float sel_u=SEL_NONE;
int detail_line=0;
int drag=MT_NONE;
int last_mx, last_my;
int detail_pos=25;
int offset_x=0, offset_y=0;
float last_w, last_ox, last_oy;
int knot_line = 0;
char texto[]="                                            ";
char nurb_file[256];

// para indicarle a idle que se movio el mouse para que marque la nueva seleccion
bool mouse_moved=false;
int mx=0,my=0;

bool draw_basis=false;
bool draw_knots=true;
bool draw_polygon=true;
bool draw_control=true;
bool draw_detail=true;
bool draw_kline=true;
bool draw_w1=true;
bool sel_on_curve=true;

int knots_nodes[ARRAY_MAX];

float BSplineBasisFunc(float knots[], int i, int k, float t) {
	if (t<knots[i] || t>knots[i+k]) return 0;
	float r;
	if (k==1)
		return (t>=knots[i] && t<knots[i+1])?1:0;
	else r=
		(t-knots[i])/((knots[i+k-1]-knots[i]==0?1:knots[i+k-1]-knots[i]))*BSplineBasisFunc(knots,i,k-1,t)
			+(knots[i+k]-t)/((knots[i+k]-knots[i+1]==0?1:knots[i+k]-knots[i+1]))*BSplineBasisFunc(knots,i+1,k-1,t);
	return r;
}

int escribir_char(const char *t, int i) {
	int p=0;
	while (t[p]!='\0') {
		texto[i]=t[p];
		p++;
		i++;
	}
	return p;

}

int escribir_int(int n, int i) {
	int p=0;
	if (n<0) {
		n=-n;
		texto[i++]='-';
		p++;
	}
	if (n>10000) {
		texto[i++]='#';
		texto[i++]='#';
		texto[i++]='#';
		texto[i++]='#';
		texto[i++]='#';
		p+=5;
	} else if (n>1000) {
		texto[i++]=n/1000+'0';
		texto[i++]=(n/100)%10+'0';
		texto[i++]=(n/10)%10+'0';
		texto[i++]=n%10+'0';
		p+=4;
	} else if (n>100) {
		texto[i++]=n/100+'0';
		texto[i++]=(n/10)%10+'0';
		texto[i++]=n%10+'0';
		p+=3;
	} else if (n>10) {
		texto[i++]=n/10+'0';
		texto[i++]=n%10+'0';
		p+=2;
	} else {
		texto[i++]=n+'0';
		p+=1;
	}
	return p;
}

int escribir_float(float f, int i) {
	int p=0;
	if (f<0) {
		f=-f;
		texto[i++]='-';
		p++;
	}
	int r = escribir_int(int(f),i);
	i+=r; p+=r;
	texto[i++]='.';
	texto[i++]=int(f*10)%10+'0';
	texto[i++]=int(f*100)%10+'0';
	p+=3;
	return p;
}

void escribir() {
	texto[0]='\0';
}

void escribir(const char *t,const char *t2) {
	int i=0;
	i += escribir_char(t,i);
	i += escribir_char(t2,i);
	texto[i]='\0';
}

void escribir(const char *t,int n) {
	int i=0;
	i += escribir_char(t,i);
	i += escribir_int(n,i);
	texto[i]='\0';
}

void escribir(const char *t,float f) {
	int i=0;
	i += escribir_char(t,i);
	i += escribir_float(f,i);
	texto[i]='\0';
}


void escribir(const char *t,int n, const char *t2, float f) {
	int i=0;
	i += escribir_char(t,i);
	i += escribir_int(n,i);
	i += escribir_char(t2,i);
	i += escribir_float(f,i);
	texto[i]='\0';
}

template<class T>
void multiply_vector( T *array, size_t size, T value ){
	for(size_t K=0; K<size; ++K)
		array[K] *= value;
}

//range []
template<class T>
bool between(T beg, T med, T end){
	return beg<=med and med<=end;
}

struct Nurb {
	GLfloat controls[ARRAY_MAX][4]; // puntos de control (x,y,z,w)
	GLfloat knots[ARRAY_MAX]; // knots
	int num; // cantidad de pts de control
	int order; // orden de la curva ( = grado+1 )
	int knum; // cantidad de knots ( = num + order )
	float detail; // tolerancia para el rasterizado de opengl
	bool cerrada;
	int knots_points[ARRAY_MAX][2]; // coord donde se dibujan los knots en la ventana
	int share_int; // auxiliar (para FindU)
	GLfloat aux_c[2][10][4]; // auxiliar (para InsertKnot)

	Nurb(const Nurb &n2) {
		detail=n2.detail;
		num=n2.num;
		knum=n2.knum;
		order=n2.order;
		cerrada=n2.cerrada;
		memcpy((void*)controls,(void*)n2.controls,sizeof(float)*4*num);
		memcpy((void*)knots,(void*)n2.knots,sizeof(float)*ARRAY_MAX);
	}

	Nurb () {
		detail=25;
		Clear();
	}

	void Move(int x, int y) {
		for (int i=0;i<num;i++) {
			float w=controls[i][W];
			controls[i][X]+=w*x;
			controls[i][Y]+=w*y;
		}
	}

	void Clear() {
		num=0;
		knum=order=4;
		for(int K=0; K<order/2; ++K)
			knots[K] = 0;
		for(int K=order/2; K<ARRAY_MAX; ++K)
			knots[K] = 1;
		/*
		knots[0]=0; knots[order-1]=1;
		int i;
		for (i=0;i<order;i++)
			knots[i]=(i-1)*1.f/(order-3);
		for (i=order-1;i<ARRAY_MAX;i++)
			knots[i]=1;
		*/
		cerrada = false;
	}



	// para invertir el sentido de la curva se el orden los ptos de control
  // y se modifica el vector de knots de forma que las distancias entre knots
  // consecutivos queden invertidas (ej { .1, .3, 1 }  -> { 0, .7, .9 }

	void InvertCurve() {
		GLfloat t;
		for (int j=0;j<4;j++) {
			for (int i=0;i<num/2;i++) {
				t=controls[i][j];
				controls[i][j]=controls[num-1-i][j];
				controls[num-i-1][j]=t;
			}
		}
		for (int i=0;i<knum/2;i++) {
			t=knots[i];
			knots[i]=1-knots[knum-1-i];
			knots[knum-i-1]=1-t;
		}
	}



	// para saber que punto esta cerca del cursor se hace va dividiendo en dos la curva
  // (para esto se insertan tantos knots como sea necesario hasta que interpole)
  // y se analiza en cual de las dos partes (o ambas) podria estar, utilizando el echo
  // de que todo punto de la curva esta dentro del convex hull de sus puntos de control,
  // y asi recursivamente se va acotando el espacio de busqueda hasta que mida 1 pixel

	float FindU(int x, int y, float tol=25) {

		if (num<order)
			return SEL_NONE;
		Nurb n2(*this);
		int k1 = n2.InsertKnot(knots[order-1],true);
		int k2 = n2.InsertKnot(knots[knum-order],true);
		float u = n2.FindUbb(n2,x,y,knots[order-1],knots[knum-order],k1-order+1,k2-order,tol);
		return u;

	}




	// esta func verifica si el pto que se busca esta dentro o cerca del bb
  // (voy a usar el bb en lugar del ch para simplificar la implementacion)
  // si el bb es suficientemente chico encontramos u, sino se divide la
  // curva en dos y se lanza recursivamente

	float FindUbb(Nurb &n2, int &x, int &y, float from, float to, int cfrom, int cto, float &tol) {

		GLfloat mx,my,Mx,My;
		mx=Mx=n2.controls[cfrom][X]/n2.controls[cfrom][W];
		my=My=n2.controls[cfrom][Y]/n2.controls[cfrom][W];

		GLfloat tx,ty;
		for (int i=cfrom;i<=cto;i++) {
			tx=n2.controls[i][X]/n2.controls[i][W];
			ty=n2.controls[i][Y]/n2.controls[i][W];
			if (ty<my) my=ty;
			if (ty>My) My=ty;
			if (tx<mx) mx=tx;
			if (tx>Mx) Mx=tx;
		}
		if (Mx-mx<=1 && My-my<=1) { // si el bb esta al limite (acotamos el pixel)
			float dx = (mx+Mx)/2-x;
			float dy = (my+My)/2-y;
			dy*=dy; dx*=dx;
			if (dx+dy<=tol) {
				tol = dx+dy;
				return (from+to)/2;
			} else {
				return SEL_NONE;
			}
		} else if (mx-tol<=x && Mx+tol>=x && my-tol<=y && My+tol>=y) { // divide & conquer
			float t = (from+to)/2;
			int nc = n2.InsertKnot(t,true);
			if (share_int) {
				float u1 = n2.FindUbb ( n2, x, y, t, to, nc-order+1, cto+share_int, tol );
				float u2 = n2.FindUbb ( n2, x, y, from, t, cfrom, nc-order+1, tol );
				return u2!=SEL_NONE?u2:u1;
			} else
				return SEL_NONE;
		}
		return SEL_NONE;

	}


	// se realiza virtualmente el algoritmo de insercion de knots tantas veces hasta que
  // la curva interpole al punto de control (que inserto dicho algoritmo), obteniendo
  // a partir de este las coordenadas del punto de la curva

	GLfloat *FindPoint(float t) { // De Boor's Algorithm

		int degree = order -1;

		int k=0; // buscar donde va el knot
		while (k<knum && knots[k]<t)
			k++;
		int s=0; // cuantas veces se repite
		while (k<knum && knots[k]==t) {
			k++; s++;
		}	
		k--;
		// corregir el extremo inferior para que no se salga
		if(degree-s<1)
			s=degree-1;
		// inicializar el vector auxiliar donde se van a guardar los pasos
	// (solo se guardan los ultimos dos, alternando entre las cols 0 y 1)
		int ar=1, lr=0, p=0, j;
		for (int i=k-degree;i<=k-s+2;i++) {
			if (i>=num)
				j=num-1;
			else
				j=i;
			aux_c[0][p][X]=controls[j][X];
			aux_c[0][p][Y]=controls[j][Y];
			aux_c[0][p][Z]=controls[j][Z];
			aux_c[0][p][W]=controls[j][W];
			p++;
		}
		// iterar hasta llegar al punto
		float a;
		for (int rr=1;rr<=degree-s;rr++) {
			p=rr;
			for (int i=k-degree+rr;i<=k-s;i++) {
				if ((knots[i+degree-rr+1]-knots[i])==0)
					a=1; // corregir el extremo superior para que no se salga
				else
					a = (t-knots[i])/(knots[i+degree-rr+1]-knots[i]);
				aux_c[ar][p][X]=(1-a)*aux_c[lr][p-1][X]+a*aux_c[lr][p][X];
				aux_c[ar][p][Y]=(1-a)*aux_c[lr][p-1][Y]+a*aux_c[lr][p][Y];
				aux_c[ar][p][Z]=(1-a)*aux_c[lr][p-1][Z]+a*aux_c[lr][p][Z];
				aux_c[ar][p][W]=(1-a)*aux_c[lr][p-1][W]+a*aux_c[lr][p][W];
				p++;
			}
			if (ar==1) { // inverts cual columna de la matrix aux_c pertence al paso actual y cual al anterior
				lr=1;ar=0;
			} else {
				lr=0; ar=1;
			}
		}
		return aux_c[lr][degree-s];
	}


	// para agregar un pto al final, se agrega el pto de control a la lista y
  // se escalan los ktnos (para mantener la forma de la curva)

	int AddControlPoint(GLfloat x, GLfloat y) {
		// order = degree+1 = knum-cnum => knum = order+cnum
		controls[num][X]=x;
		controls[num][Y]=y;
		controls[num][Z]=0;
		controls[num][W]=1;
		for (int i=0;i<knum-1;i++) {
			knots[i]*=1-1.f/(knum-2);
		}
		num++; knum++;
		return num-1;
	}


	// algoritmo de De Boor
  // interpolate = true inserta tantas veces como sea necesario hasta que haya
  // un pto de control en esa posicion de la curva (continuidad c0)
  // interpolate = false agrega una sola vez siempre y cuando la cantidad de veces
  // que ya esta presente no sea mayor al orden (se puede asi llegar a perder
  // la continuidad c0, pero luego ya no tiene sentido seguir insertando)
  // esto modifica el poligono de control, no la curva

	int InsertKnot(float t, bool to_interpolate = false) {

		share_int = 0;
		int k=0; // buscar donde va el knot
		int s=0; // y cuantas veces ya esta repetido si esta
		while (k<knum && knots[k]<t)
			k++;
		while (k<knum && knots[k]==t) {
			k++;
			s++;
		}
		if (k!=0) k--;

		if (s>=order-(to_interpolate?1:0))
			return k;


		do {
			share_int++;
			int i;
			// hacer lugar para el nuevo knot
			for (i=knum;i>k;i--)
				knots[i+1]=knots[i];
			// insertar el knot
			knots[k+1]=t;

			// hacer lugar para el nuevo punto de control
			for (i=knum;i>k;i--) {
				controls[i][X]=controls[i-1][X];
				controls[i][Y]=controls[i-1][Y];
				controls[i][Z]=controls[i-1][Z];
				controls[i][W]=controls[i-1][W];
			}
			// acomodar los pts de control que corresponda
			GLfloat lc[4], tmp;
			lc[Y]=controls[k-order][X];
			lc[X]=controls[k-order][Y];
			lc[Z]=controls[k-order][Z];
			lc[W]=controls[k-order][W];
			for (i=k-order+1;i<=k;i++) {
				for (int j=0;j<4;j++) {
					float a = (t-knots[i]) / (knots[i+order]-knots[i]) ;
					tmp=controls[i][j];
					controls[i][j]=(1-a)*lc[j]+a*tmp;
					lc[j]=tmp;
				}
			}

			knum++;
			num++;
			k++;
			s++;
		} while (to_interpolate && s+1<order);
		return k;
	}


	// mueve knots de forma tal que la curva se "deforme" para interpolar el
  // pto de control

	void Interpolate(int c) {

		int i=c+1;
		if (i<0)
			i=0;
		float l1=0,l2=1;
		if (i>0)
			l1=knots[i-1];
		if (i+order<knum)
			l2=knots[i+order];
		float kn=(l2+l1)/2;
		for (int j=0;j<order-1;j++) {
			knots[i+j]=kn;
		}
	}


	// cambia el grado de la curva (sin consideraciones, la curva cambia)

	void SetDegree(int n) {
		order=n+1;
		knum=order+num;
		ResetKnots(true);
	}


	// elimina un pto de control y su knot "correspondiente"

	void DeleteControl(int n) {
		num--;
		knum--;
		int i;
		for (i=n;i<num;i++) {
			controls[i][X]=controls[i+1][X];
			controls[i][Y]=controls[i+1][Y];
			controls[i][Z]=controls[i+1][Z];
			controls[i][W]=controls[i+1][W];
		}
		for (i=n;i<knum;i++)
			knots[i]=knots[i+1];
		knots[knum]=1;
	}


	// reacomoda los knots equiespaciadamente
  // o interpolando los extremos y en medio equiespaciadamente

	void ResetKnots(bool interpolate=false) {
		int c=0;
		if (interpolate) {
			while (c<order)
				knots[c++]=0;
			while (c<knum-order) {
				knots[c]=float(c-order+1)/(knum-order-order+1);
				c++;
			}
			while (c<knum)
				knots[c++]=1;
		} else {
			for (int i=1;i<knum-1;i++)
				knots[i]=(i-1)*(1.f/(knum-3));
		}
	}

	// simulan el zoom aplicando un factor de escala a las coord de los pts de controls
  // y desplazandolos para que el mouse quede en el mismo lugar con respecto a la curva

	void ZoomIn(int x, int y) {
		for (int i=0;i<num;i++) {
			float w=controls[i][W];
			controls[i][X] = (controls[i][X]/w*ZOOM_FACTOR + (x-x*ZOOM_FACTOR))*w;
			controls[i][Y] = (controls[i][Y]/w*ZOOM_FACTOR + (y-y*ZOOM_FACTOR))*w;
		}
	}
	void ZoomOut(int x, int y) {
		for (int i=0;i<num;i++) {
			float w=controls[i][W];
			controls[i][X] = (controls[i][X]/w/ZOOM_FACTOR + (x-x/ZOOM_FACTOR))*w;
			controls[i][Y] = (controls[i][Y]/w/ZOOM_FACTOR + (y-y/ZOOM_FACTOR))*w;
		}
	}

	//crea un ciclo con los puntos de control y los knots
	//se insertan copias al final de los primeros n=order puntos
	//para los knots se insertan los desplazamientos
	//escalados segun la primera y ultima diferencias
	void Cerrar(){
		for(int K=num, L=0; L<order; ++K, ++L){
			controls[K][X]=controls[L][X];
			controls[K][Y]=controls[L][Y];
			controls[K][Z]=controls[L][Z];
			controls[K][W]=controls[L][W];
		}
		num += order;

		double factor = (knots[knum-2]-knots[knum-3]) / (knots[2]-knots[1]);
		for(int K=knum-1, L=order-1; L<2*order; ++K, ++L){
			knots[K] = knots[K-1] + factor*(knots[L] - knots[L-1]);
		}
		knum += order;

		//normalizar, no es necesario para definir la curva
		//se usa para dibujar el knot vector
		for(int K=0; K<knum; ++K)
			knots[K] /= knots[knum-2];
		knots[knum-1] = 1;

		cerrada = true;
	}

	void Abrir(){
		if( not cerrada ) return;
		//eliminar los puntos que creaban el ciclo;
		num -= order;
		knum -= order;

		//normalizar, no es necesario para definir la curva
		//se usa para dibujar el knot vector
		for(int K=0; K<knum; ++K)
			knots[K] /= knots[knum-2];
		knots[knum-1] = 1;

		cerrada = false;
	}


	// dibuja la curva, el poligono de control y los pts

	void Dibujar() {

		if (draw_control) { // draw control points
			glColor3fv(color_cpoint);
			glPointSize(4);
			glBegin(GL_POINTS);
			for (int i=0;i<num;i++){
				float w=controls[i][W];
				if (w>0) glVertex4fv(controls[i]);
				else glVertex4f(-controls[i][X],-controls[i][Y],0,-controls[i][W]);
			}
			glEnd();
		}

		if (draw_polygon) { // draw control polygon
			glLineWidth(1);
			glColor3fv(color_cline);
			glBegin(GL_LINE_STRIP);
			for (int i=0;i<num;i++)
				glVertex4fv(controls[i]);
			glEnd();
		}

		//verificar que se pueda definir la curva
		if( num<order ) return;

		if (draw_w1 and num) { // dibujar la curva ignorando W
			int i;
			for (i=0;i<num;i++){
				float w=controls[i][W];
				if(w not_eq 0){
					controls[i][X]/=w;
					controls[i][Y]/=w;
				}
			}

			for(int K=num; K<ARRAY_MAX; ++K)
				controls[K][W] = 1;

			for(int K=knum; K<ARRAY_MAX; ++K)
				knots[K] = 1;

			glEnd();
			glLineWidth(1);
			glColor3fv(color_nurb);
			GLUnurbs *oNurb = gluNewNurbsRenderer();
			gluNurbsProperty(oNurb, GLU_SAMPLING_TOLERANCE, detail);

			gluBeginCurve(oNurb);{
				//gluNurbsCurve(oNurb, knum, knots, 4, (float*)controls, order, GL_MAP1_VERTEX_3);
				gluNurbsCurve(
					oNurb,
					knum, knots,
					4, &controls[0][0],
					order, //order
					GL_MAP1_VERTEX_3
				);
			}gluEndCurve(oNurb);

			for (i=0;i<num;i++){
				float w=controls[i][W];
				if(w not_eq 0){
					controls[i][X]*=w;
					controls[i][Y]*=w;
				}
			}
		}

		if(num){
			// dibujar la curva real
			glLineWidth(2);
			glColor3fv(color_nurb);
			GLUnurbs *oNurbW = gluNewNurbsRenderer();
			gluNurbsProperty(oNurbW, GLU_SAMPLING_TOLERANCE, detail);
			gluBeginCurve(oNurbW);
			gluNurbsCurve(oNurbW, knum, knots, 4, (float*)controls, order, GL_MAP1_VERTEX_4);
			gluEndCurve(oNurbW);
		}

		// dibujar los knot sobre la curva
	// (por alguna razon los de los extremos (p primeros y p ultimos) no tienen sentido, asi que no se dibujan
		if (draw_knots) {
			float *p;
			glColor3fv(color_knot);
			glPointSize(5);
			glBegin(GL_POINTS);
			for (int i=order;i<knum-order;i++) {
				p=FindPoint(knots[i]);
				glVertex4fv(p);
				knots_points[i][X]=int(p[X]/p[W]);
				knots_points[i][Y]=int(p[Y]/p[W]);
			}
			glEnd();
		}


	}


	// guardar/cargar los datos de la nurb en un archivo de texto
  // (es el que se le pasa como parametro al ejecutable)
	void Save(char *fname) {
		ofstream fil(fname,ios::trunc);
		fil<<"order "<<order<<endl;
		fil<<"pts "<<num<<endl;
		int i;
		for (i=0;i<num;i++)
			fil<<controls[i][X]<<" "<<controls[i][Y]<<" "<<controls[i][Z]<<" "<<controls[i][W]<<endl;
		fil<<"knots "<<knum<<endl;
		for (i=0;i<knum;i++)
			fil<<knots[i]<<endl;
		fil<<"detail "<<detail<<endl;
		fil.close();
	}
	bool Load(char *fname) {
		ifstream fil(fname);
		if (fil.is_open()) {
			string s;
			fil>>s>>order;
			fil>>s>>num;
			int i;
			for (i=0;i<num;i++)
				fil>>controls[i][X]>>controls[i][Y]>>controls[i][Z]>>controls[i][W];
			fil>>s>>knum;
			for (i=0;i<knum;i++)
				fil>>knots[i];
			fil>>s>>detail;
			return true;
		}
		return false;
	}

} nurb;


// callback del menu contextual del mouse
void menu_cb(int value) {

}

// callback del display de la ventana de imagenes
void display_cb() {
	glClear(GL_COLOR_BUFFER_BIT);

	if (offset_x!=0 || offset_y!=0) { // desplazar si esta pendiente un panning
		nurb.Move(offset_x,offset_y);
		last_ox-=offset_x;
		last_oy-=offset_y;
		offset_x=0;
		offset_y=0;
	}


	nurb.Dibujar();

	// marcar el pto de control seleccionado
	if (sel_control!=SEL_NONE) {
		glColor3fv(color_cpoint);
		glPointSize(7);
		float* p=nurb.controls[sel_control];
		glBegin(GL_POINTS);
		float w=p[W];
		if (w>0) glVertex4fv(p);
		else glVertex4f(-p[X],-p[Y],0,-p[W]);
		glEnd();
	}

	if (draw_detail) {
		// draw detail line
		glLineWidth(1);
		glColor3fv(color_detail);
		glBegin(GL_LINES);
		glVertex2i(w-MARGIN, MARGIN);
		glVertex2i(w-MARGIN,h-MARGIN-MARGIN);
		glEnd();

		// draw detail point
		glPointSize(sel_detail?10:7);
		glBegin(GL_POINTS);
		glVertex2f(w-MARGIN,detail_pos);
		glEnd();
	}

	if (draw_basis) {
		glColor3fv(color_knot);
		for (int i=0;i<nurb.num;i++) { // por cada pt de control
			glLineWidth(sel_control==i?2:1);
			glBegin(GL_LINE_STRIP);
			float tmax=w-MARGIN-MARGIN,y;
			for (float t=0;t<=tmax;t++) {
				y=BSplineBasisFunc(nurb.knots,i,nurb.order,t/tmax);
				glVertex2f(MARGIN+t,h-MARGIN*2-y*h/3);
			}
			glEnd();			
		}
	}

	if (draw_kline) {
		// draw knots line
		glLineWidth(1);
		glColor3fv(color_knot);
		glBegin(GL_LINES);
		glVertex2i(MARGIN,h-MARGIN);
		glVertex2i(w-MARGIN,h-MARGIN);
		glEnd();

		// draw knots
		int kh=h-MARGIN;
		glPointSize(4);
		glBegin(GL_POINTS);
		float lk=-1;
		int count=0;
		for (int i=1;i<nurb.knum-1;i++) {
			if (lk==nurb.knots[i]) {
				knots_nodes[i]=knots_nodes[i-1];
				count++;
			} else {
				if (count) {
					glEnd();
					glRasterPos2f(MARGIN+knot_line*lk-10,h-MARGIN-5);
					glutBitmapCharacter(font, 'x');
					glutBitmapCharacter(font, count+'1');
					glBegin(GL_POINTS);
					count=0;
				}
				glVertex2i(knots_nodes[i] = int(MARGIN+knot_line*(lk=nurb.knots[i])),kh);
			}
		}
		glEnd();
		if (count) {
			glRasterPos2f(MARGIN+knot_line*lk-10,h-MARGIN-5);
			glutBitmapCharacter(font, 'x');
			glutBitmapCharacter(font, count+'1');
		}
		if (sel_knot==SEL_NEW) { // dibujar el knot seleccionado
			glColor3fv(color_new);
			glPointSize(7);
			glBegin(GL_POINTS);
			glVertex2i(int(sel_x),kh);
			glVertex4fv(nurb.FindPoint(float(sel_x-MARGIN)/knot_line));
			glEnd();
		} else if (sel_knot!=SEL_NONE) {
			glPointSize(7);
			glBegin(GL_POINTS);
			glVertex2i(knots_nodes[sel_knot],kh);
			glVertex4fv(nurb.FindPoint(nurb.knots[sel_knot]));
			glEnd();
		}
	} else if (sel_on_curve) {
		if (sel_knot==SEL_NEW) { // dibujar el knot seleccionado
			glColor3fv(color_new);
			glPointSize(7);
			glBegin(GL_POINTS);
			glVertex4fv(nurb.FindPoint(float(sel_x-MARGIN)/knot_line));
			glEnd();
		} else if (sel_knot!=SEL_NONE) {
			glColor3fv(color_knot);
			glPointSize(7);
			glBegin(GL_POINTS);
			glVertex4fv(nurb.FindPoint(nurb.knots[sel_knot]));
			glEnd();
		}
	}

	sel_u=nurb.FindU(mx,my);
	if (sel_u>0){
		//porcion de la curva que representa
		int K=0;
		for(; K<ARRAY_MAX and nurb.knots[K]<sel_u; ++K)
			;

		glLineWidth(5);
		glColor3fv(color_cline);
		glBegin(GL_LINE_STRIP);

		for (int i=K-1;i>=0 and K-1-i<nurb.order;i--)
			glVertex4fv(nurb.controls[i]);

		glEnd();
	}


	int i=0;
	glColor3fv(color_texto);
	glRasterPos2f(MARGIN,MARGIN);
	while (texto[i]!='\0')
		glutBitmapCharacter(font, texto[i++]);

	glutSwapBuffers();
}



void idle_cb() {
	usleep(1000);
	int x=mx, y=my;
	if (mouse_moved && drag==MT_NONE) { // si no estamos haciendo nada marcar la seleccion si hay algo debajo del cursor
		if (draw_detail && x>w-MARGIN-10 && x<w-MARGIN+10 && y>detail_pos-10 && y<detail_pos+10) {
			sel_control=sel_knot=SEL_NONE;
			sel_detail=true;
			escribir("tol = ",nurb.detail);
			glutPostRedisplay();
		} else {
			if (sel_detail) {
				escribir();
				sel_detail=false;
			}
			int d1,d2;
			if (draw_kline && y<h-MARGIN+10 && y>h-MARGIN-10) { // seleccion en la linea de los knots
				int md=5;
				int m_ksel=SEL_NONE;
				sel_control=SEL_NONE;
				for (int i=0;i<nurb.knum;i++) { // buscar el knot mas cercano
					d1 = knots_nodes[i]>x? knots_nodes[i]-x : x-knots_nodes[i];
					if (d1<md) {
						md=d1;
						m_ksel=i;
					}
				}
				if (m_ksel!=SEL_NONE) { // si esta repetido, buscar el de "afuera"
					if (x>knots_nodes[m_ksel]) {
						while (m_ksel+1<nurb.knum && knots_nodes[m_ksel]==knots_nodes[m_ksel+1])
							m_ksel++;
					} else {
						while (m_ksel>0 && knots_nodes[m_ksel]==knots_nodes[m_ksel-1])
							m_ksel--;
					}

					if (sel_knot!=m_ksel) {
						sel_knot = m_ksel;
						escribir("knot ",m_ksel,"  -  u = ",nurb.knots[m_ksel]);
					}

				} else if (x>=MARGIN && x<=w-MARGIN) { // si no hay ninguno marcar la posicion para agregar
					if (x!=sel_x || sel_knot!=SEL_NEW) {
						escribir("insertar knot  -  u = ",float(sel_x-MARGIN)/knot_line);
						sel_knot=SEL_NEW;
						sel_x=x;
					}
				} else if (sel_control!=SEL_NONE || sel_knot!=SEL_NONE) {
					sel_control=sel_knot==SEL_NONE;
					escribir();
				}
			} else if (draw_control){ // seleccion en el polinomio de control
				int md=10;
				int m_csel=SEL_NONE;
				for (int i=0;i<nurb.num;i++) {
					float wi=nurb.controls[i][W],xi=nurb.controls[i][X]/wi,yi=nurb.controls[i][Y]/wi;
					d1 = int(xi<x?x-xi:xi-x);
					d2 = int(yi<y?y-yi:yi-y);
					if (d1<md && d2<md) {
						m_csel=i;
						md = d1>d2?d2:d1;
					}
				}
				if (m_csel!=sel_control) { // si cambio la seleccion, actualizar
					sel_control=m_csel;
					sel_knot=SEL_NONE;
					if (sel_control==SEL_NONE)
						escribir();
					else
						escribir("pto control ",m_csel,"  -  w = ",nurb.controls[m_csel][W]);
				}
				sel_knot=SEL_NONE;
				if (sel_on_curve && draw_knots && m_csel==SEL_NONE) {
					for (int i=nurb.order;i<nurb.knum;i++) {
						md=5;
						d1 = nurb.knots_points[i][X]-x;
						d2 = nurb.knots_points[i][Y]-y;
						if (d1>=-md && d1<=md && d2>=-md && d2<=md) {
							sel_knot=i;
							if (d1<0)
								d1=-d1;
							if (d2<0)
								d2=-d2;
							md = d1<d2?d1:d2;
						}
					}
					if (sel_knot==SEL_NONE) {
						sel_u=nurb.FindU(x,y);
						if (sel_u!=SEL_NONE) {
							sel_knot=SEL_NEW;
							sel_x=MARGIN+knot_line*sel_u;
							escribir("t = ",sel_u);
							sel_control=SEL_NONE;
						}
					}
				}
			} else {
				sel_control=sel_knot=SEL_NONE;
				sel_u=nurb.FindU(x,y);
				if (sel_u!=SEL_NONE) {
					sel_knot=SEL_NEW;
					sel_x=MARGIN+knot_line*sel_u;
					escribir("t = ",sel_u);
					sel_control=SEL_NONE;
				}
			}
			glutPostRedisplay();
		}
		mouse_moved=false;
	}

}


// callback del motion de la ventana de imagenes
void motion_cb(int x, int y) {
	mx=x;my=y;
	mouse_moved=true;
	if (drag==MT_CONTROL) { // si se esta arrastrando un punto de control
		nurb.controls[sel_control][X]=x*nurb.controls[sel_control][W];
		nurb.controls[sel_control][Y]=y*nurb.controls[sel_control][W];

		if( nurb.cerrada ){
			int K = -1;
			if( between( 0, sel_control, nurb.order-1 ) ){
				K = nurb.num - nurb.order + sel_control;
			}
			else if( between( nurb.num-nurb.order, sel_control, nurb.num-1 ) ){
				K = sel_control - (nurb.num-nurb.order);
			}
			if( K not_eq -1 ){
				nurb.controls[K][X] = nurb.controls[sel_control][X];
				nurb.controls[K][Y] = nurb.controls[sel_control][Y];
				nurb.controls[K][Z] = nurb.controls[sel_control][Z];
				nurb.controls[K][W] = nurb.controls[sel_control][W];
			}
		}
		glutPostRedisplay();
	} else if (drag==MT_COORD_W) { // si se esta cambiando la w de un pto de control
		float oldW=nurb.controls[sel_control][W], newW=last_w+float(last_my-y)/20;
		if (newW==0) newW=1e-10;
		nurb.controls[sel_control][X]*=newW/oldW;
		nurb.controls[sel_control][Y]*=newW/oldW;
		nurb.controls[sel_control][W]=newW;
		glutPostRedisplay();
		escribir("pto control ",sel_control,"  -  w = ",nurb.controls[sel_control][W]);
	} else if (drag==MT_KNOT) { // si se esta arrastrando un knot
		float p=float(x-MARGIN+sel_x)/(knot_line);
		if (p<0) p=0;
		else if (p>1) p=1;
		if (sel_knot>0 && nurb.knots[sel_knot-1]>p)
			nurb.knots[sel_knot]=nurb.knots[sel_knot-1];
		else if (sel_knot+1<nurb.knum && nurb.knots[sel_knot+1]<p)
			nurb.knots[sel_knot]=nurb.knots[sel_knot+1];
		else
			nurb.knots[sel_knot]=p;

		if( nurb.cerrada ){
			int K=-1;
			if( between( 1, sel_knot, nurb.order+1 ) ){
				K = nurb.knum - nurb.order + sel_knot - 2;
			}
			else if( between( nurb.knum-nurb.order, sel_knot, nurb.knum-1 ) ){
				K = sel_knot - (nurb.knum-1-nurb.order);
			}

			if( K not_eq -1 ){
				nurb.knots[K] = nurb.knots[K-1] + nurb.knots[sel_knot] - nurb.knots[sel_knot-1];
			}
			multiply_vector( nurb.knots, nurb.knum, 1/nurb.knots[nurb.knum-2] );
			nurb.knots[nurb.knum-1] = 1;
		}
		glutPostRedisplay();
		escribir("knot ",sel_knot, "  -  u = ",nurb.knots[sel_knot]);
	} else if (drag==MT_DETAIL) { // si se esta cambiando la tolerancia para el dibujo
		float d = float(y-MARGIN)/detail_line;
		if (d>=0) {
			nurb.detail=d*MAX_DETAIL;
			detail_pos = y;
		}
		escribir("tol = ",nurb.detail+1);
		glutPostRedisplay();
	} else if (drag==MT_MOVE) {
		offset_x = int(last_ox+(x-last_mx));
		offset_y = int(last_oy+(y-last_my));
		glutPostRedisplay();
	}
}

// callback del mouse de la ventana de imagenes
void mouse_cb(int button, int state, int x, int y){
	if (button==GLUT_LEFT_BUTTON) {
		if (state==GLUT_DOWN) {
			if (drag==MT_NONE) {
				if (sel_detail)
					drag = MT_DETAIL;
				else if (sel_control!=SEL_NONE) {
					if (sel_control!=SEL_NEW) // mover un punto de control
						drag = MT_CONTROL;
				} else if (sel_knot!=SEL_NONE) {
					if (sel_knot!=SEL_NEW) { // mover un knot existente
						drag = MT_KNOT;
						sel_x=MARGIN+knot_line*nurb.knots[sel_knot]-x;
					} else if (sel_knot==SEL_NEW) { // insertar un nuevo knot
						drag = MT_KNOT;
						sel_knot = nurb.InsertKnot(float(sel_x-MARGIN)/knot_line);
						sel_x=MARGIN+knot_line*nurb.knots[sel_knot]-x;
						glutPostRedisplay();
					}
				} else { // agregar un punto de control
					escribir("pto control ",nurb.num);
					drag=MT_CONTROL;
					sel_control = nurb.AddControlPoint(x,y);
					glutPostRedisplay();
				}
			}
		} else {
			drag = MT_NONE;
			motion_cb(x,y);
		}
	} else 	if (button==3) {
		nurb.ZoomIn(x,y);
		display_cb();
	} else 	if (button==4) {
		nurb.ZoomOut(x,y);
		display_cb();
	} else 	if (button==GLUT_RIGHT_BUTTON) {
		if (state==GLUT_DOWN) {
			if (sel_control!=SEL_NONE) {
				last_my = y;
				last_w = nurb.controls[sel_control][W];
				drag = MT_COORD_W;
			} else {
				sel_control=sel_knot=SEL_NONE;
				sel_detail=false;
				last_mx=x; last_my=y;
				last_ox = offset_x;
				last_oy = offset_y;
				drag=MT_MOVE;
			}
		} else {
			if (drag==MT_COORD_W || drag==MT_MOVE)
				drag = MT_NONE;
		}
	}

}

void reshape_cb(int aw, int ah){
	w=aw;h=ah;

	if (!h||!w) {
		minimized=true;
		return;
	} else
		minimized=false;

	glViewport(0,0,w,h); // region donde se dibuja
	knot_line=w-30;
	detail_line=h-MARGIN*3;
	detail_pos = int(MARGIN+(nurb.detail-1)/MAX_DETAIL*detail_line);

	// matriz de proyeccion
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(0,w,h,0,-1,1);

	glutPostRedisplay();
}

// callback del teclado
void keyboard_cb(unsigned char key,int x=0,int y=0) {
	if (key==27) {
		exit(0);
	} else if (key=='k') {
		draw_knots=!draw_knots;
		sel_u=sel_knot=SEL_NONE;
		glutPostRedisplay();
	} else if (key=='d') {
		draw_detail=!draw_detail;
		sel_detail=false;
		glutPostRedisplay();
	} else if (key=='l') {
		draw_kline=!draw_kline;
		sel_u=sel_knot=SEL_NONE;
		glutPostRedisplay();
	} else if (key=='f') {
		draw_basis=!draw_basis;
		glutPostRedisplay();
	} else if (key=='g') {
		draw_polygon=!draw_polygon;
		glutPostRedisplay();
	} else if (key=='p') {
		draw_control=!draw_control;
		sel_u=sel_control=SEL_NONE;
		glutPostRedisplay();
	} else if (key=='w') {
		draw_w1=!draw_w1;
		glutPostRedisplay();
	} else if (key=='b') {
		sel_u=sel_knot=sel_control=SEL_NONE;
		nurb.Clear();
		glutPostRedisplay();
	} else if (key==127 && sel_control!=SEL_NONE) {
		nurb.DeleteControl(sel_control);
		sel_control=SEL_NONE;
		glutPostRedisplay();
	} else if (key=='u' && sel_control!=SEL_NONE) {
		nurb.controls[sel_control][W]=1;
		escribir("pto control ",sel_control,"  -  w = ",1);
		glutPostRedisplay();
	} else if (key=='i' && sel_control!=SEL_NONE) {
		nurb.Interpolate(sel_control);
		glutPostRedisplay();
	} else if (key=='i' && sel_u!=SEL_NONE) {
		nurb.InsertKnot(sel_u,true);
		glutPostRedisplay();
	} else if (key>='1' && key<='9') {
		nurb.SetDegree(key-'0');
		sel_u=sel_knot=sel_control=SEL_NONE;
		escribir("grado = ",key-'0');
		glutPostRedisplay();
	} else if (key=='s') {
		nurb.Save(nurb_file);
		escribir("nurb guardada: ",nurb_file);
		glutPostRedisplay();
	} else if (key=='n') {
		sel_knot=sel_control=SEL_NONE;
		sel_u=SEL_NONE;
		nurb.ResetKnots(false);
		glutPostRedisplay();
	} else if (key=='m') {
		sel_u=sel_knot=sel_control=SEL_NONE;
		nurb.ResetKnots(true);
		glutPostRedisplay();
	} else if (key=='x' && sel_knot!=SEL_NONE) {
		float pos = sel_knot==SEL_NEW? float(sel_x-MARGIN)/knot_line : nurb.knots[sel_knot];
		sel_knot = nurb.InsertKnot(pos);
		glutPostRedisplay();
	} else if (key=='c') {
		sel_on_curve=!sel_on_curve;
		sel_u=SEL_NONE;
		glutPostRedisplay();
	} else if (key=='r') {
		nurb.InvertCurve();
		sel_u=sel_knot=sel_control=SEL_NONE;
		glutPostRedisplay();
	} else if (key=='+') {
		sel_control=SEL_NONE;
		sel_u=SEL_NONE;
		nurb.ZoomIn(x,y);
		glutPostRedisplay();
	} else if (key=='-') {
		sel_control=SEL_NONE;
		sel_u=SEL_NONE;
		nurb.ZoomOut(x,y);
		glutPostRedisplay();
	} else if (key=='C'){
		nurb.Cerrar();
	} else if (key=='A'){
		nurb.Abrir();
	}
	glutPostRedisplay();
}

void show_help() {
	cout<<"\nAyuda rapida: \n\n";
	cout<<"  click izquierdo en el aire: agregar punto de control\n";
	cout<<"  click izquierdo en un pto de control: mover punto de control\n";
	cout<<"  click izquierdo en un knot en la linea del vector de knots: mover knot\n";
	cout<<"  tecla x o click izquierdo en la linea del vector de knots: agregar un knot\n";
	cout<<"  click derecho en el aire: mover toda curva\n";
	cout<<"  click derecho en un pto de control: cambiar coordenada W del punto\n";
	cout<<"  teclas 1 a 9: seleccionar grado de la nurb (no mantiene la forma)\n";
	cout<<"  tecla b: borrar la nurb\n";
	cout<<"  tecla DEL: quitar pto de control seleccionado\n";
	cout<<"  tecla u: setear W a 1 en el pto de control seleccionado\n";
	cout<<"  tecla i: reacomoda algunos knots para interpolar el pto de control selec.\n";
	cout<<"  tecla n y m: redistribuir knots equiespaciadamente o interpolando extremos\n";
	cout<<"  tecla p: mostrar/ocultar los puntos de control\n";
	cout<<"  tecla g: mostrar/ocultar el poligono de control\n";
	cout<<"  tecla d: mostrar/ocultar el control de tolerancia para el rasterizado\n";
	cout<<"  tecla l: mostrar/ocultar la linea del vector de knots\n";
	cout<<"  tecla k: mostrar/ocultar los knots sobre la curva\n";
	cout<<"  tecla f: mostrar/ocultar las funciones base\n";
	cout<<"  tecla c: permitir/no permitir seleccionar un knot sobre la curva\n";
	cout<<"  tecla r: invertir el orden de los puntos en la curva sin alterarla\n";
	cout<<"  teclas + y -: zoom in y out en torno al cursor del mouse\n";
	cout<<"  tecla s: guarda la nurb en el archivo que se paso por parametro (o nurb.nurb)\n";
}

void init() {
	glutInitDisplayMode(GLUT_RGB|GLUT_DOUBLE);

	glutInitWindowSize(w,h);
	glutInitWindowPosition(100,100);
	glutCreateWindow("Nurbs Demo");
	glMatrixMode(GL_PROJECTION);
	glOrtho(0,w,0,h,1,1);
	minimized=false;


	glutDisplayFunc(display_cb);
	glutMouseFunc(mouse_cb);
	glutKeyboardFunc(keyboard_cb);
	glutMotionFunc(motion_cb);
	glutPassiveMotionFunc(motion_cb);
	glutReshapeFunc(reshape_cb);
	glutIdleFunc(idle_cb);

	knot_line=w-MARGIN*2;
	detail_line=h-MARGIN*3;
	detail_pos = int(MARGIN+(nurb.detail-1)/MAX_DETAIL*detail_line);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	glClearColor(color_fondo[0],color_fondo[1],color_fondo[2],1);

}

int main(int argc, char *argv[]){
	glutInit(&argc,argv); // inicializa glut
	init();
	if (argc==1) {
		strcpy(nurb_file,"nurb.nurb");
	} else {
		strcpy(nurb_file,argv[1]);
		nurb.Load(nurb_file);
	}
	show_help();
	glutMainLoop();
	return 0;
}

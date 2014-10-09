#include <iostream>
#include <fstream>
#include <string>
#include <GL/glut.h>
#include <vector>

namespace{ //globales
	std::string file; //eliminar
	const int knot_line = 0;
	float
		color_fondo[] = {0,0,0,0},
		color_cline[]={.2f,.2f,.4f}, // poligono de control
		color_cpoint[]={.3f,0.2f,0.8f}, // puntos de control
		color_nurb[]={.8f,.6f,.6f}, // curva
		color_knot[]={.4f,0.f,0.f}, // line de knots
		color_texto[]={.3f,0.2f,0.8f}, // eje
		color_detail[]={.6f,.8f,.6f}, // lineas accesorias
		color_new[]={.8f,.6f,.6f}; // nuevo nodo
	int w,h;
	const int margin = 1;
}

void show_help();
void init();
void display_cb();
void mouse_cb(int button, int state, int x, int y);
void keyboard_cb(unsigned char key,int x,int y) ;
void motion_cb(int x, int y) ; 
void reshape_cb(int aw, int ah);
void idle_cb() ;

class Point{
	public:
		typedef float value_type; //las funciones de OpenGL requieren float
		Point(value_type x, value_type y){ //puntos en el plano
			this->x[0] = x;
			this->x[1] = y;
			this->x[2] = 0; 
			this->x[3] = 1;
		}
		value_type x[4];
};

class Nurbs{
	public:
	std::vector<Point> control;
	int order;

	Nurbs(): order(4){}

	bool dibujar(){
		glPushAttrib(GL_ALL_ATTRIB_BITS);
		//poligono de control
		glColor3fv(color_cpoint);
		glBegin(GL_POINTS);{
			for(int K=0; K<control.size(); ++K)
				glVertex4fv( control[K].x );
		}glEnd();
		glBegin(GL_LINE_STRIP);{
			for(int K=0; K<control.size(); ++K)
				glVertex4fv( control[K].x );
		}glEnd();

		//no hay suficientes puntos para definir la curva
		if(control.size() < order)
			return false;

		//dibuja nurbs con knots distruidos uniformemente
		int n = control.size() + order;
		std::vector<Point::value_type> knots(n);
		//distribucion uniforme 
		//TODO: permitir cambiar la distribución
		for(int K=0; K<n; ++K)
			knots[K] = K*1.0/(n-1);

		for(int K=0; K<n; ++K)
			std::cerr << knots[K] << ' ';
		std::cerr << '\n';


		glColor3fv(color_nurb);
		glLineWidth(1);
		GLUnurbs *oNurb = gluNewNurbsRenderer();
		gluNurbsProperty(oNurb, GLU_SAMPLING_TOLERANCE, 1.0); //?resolucion?
		gluBeginCurve(oNurb);{
			gluNurbsCurve(
				oNurb,
				knots.size(), knots.data(),
				4, (Point::value_type*) control.data(), //stride and control points
				order,
				GL_MAP1_VERTEX_3
			);
		}gluEndCurve(oNurb);

		glPopAttrib();
		gluDeleteNurbsRenderer(oNurb); //TODO: encapsular el New/Delete
		return true;
	}

	void add_control_point( Point p ){
		this->control.push_back(p);
	}

	void increase_order(){
		++order;
	}

	void decrease_order(){
		if(order > 2)
			--order;
	}

};

namespace{
	Nurbs nurbs;
}

int main(int argc, char *argv[]){
	glutInit(&argc,argv); // inicializa glut
	init();
	if (argc==1) 
		file = "default.nurb";
	else{
		file = argv[1];
		//nurbs.Load(file);
	}

	show_help();
	glutMainLoop(); 
	return 0;
}

void init() {
	glutInitDisplayMode(GLUT_RGB|GLUT_DOUBLE);

	glutCreateWindow("nurbs demo");

	glutDisplayFunc(display_cb);
	glutMouseFunc(mouse_cb);
	glutKeyboardFunc(keyboard_cb);
	//glutMotionFunc(motion_cb);
	//glutPassiveMotionFunc(motion_cb);
	glutReshapeFunc(reshape_cb);
	//glutIdleFunc(idle_cb);

	glMatrixMode(GL_MODELVIEW); 
	glLoadIdentity();
	glClearColor(color_fondo[0], color_fondo[1], color_fondo[2], color_fondo[3]);
}

void display_cb() {
	glClear(GL_COLOR_BUFFER_BIT);
	glColor3d(0,1,0);
	glPointSize(10);

	for(int K=-2; K<=2; ++K)
		for(int L=-2; L<=2; ++L){
			glBegin(GL_POINTS);{
				glVertex2d(K*.5,L*.5);
			}glEnd();
		}
	nurbs.dibujar();
	glutSwapBuffers();
}

	void show_help() {
		using std::cout;
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
		cout<<"  tecla a/A: aumentar/disminuir el order\n";
		cout<<"  tecla r: invertir el orden de los puntos en la curva sin alterarla\n";
		cout<<"  teclas + y -: zoom in y out en torno al cursor del mouse\n";
		cout<<"  tecla s: guarda la nurb en el archivo que se paso por parametro (o nurb.nurb)\n";
	}

void mouse_cb(int button, int state, int x, int y){
	double TOL=5;
	double
		xx = x*2.0/w - 1,
		yy = -y*2.0/h + 1;
	if( button==GLUT_LEFT_BUTTON ){
		if( state==GLUT_DOWN ){
			//agrega puntos de control
			nurbs.add_control_point( Point(xx,yy) );
			glutPostRedisplay();
		}
	}
}

void reshape_cb(int aw, int ah){
	w=aw;h=ah;

	if (!h||!w)
		return;

	glViewport(0,0,w,h); // region donde se dibuja

	// matriz de proyeccion
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();

	glutPostRedisplay();
}

void keyboard_cb(unsigned char key,int x,int y) {
	if(key == 'a')
		nurbs.increase_order();
	else if(key == 'A')
		nurbs.decrease_order();

	std::cerr << "order: " << nurbs.order << '\n';
	glutPostRedisplay();
}

import React from 'react';
import PropTypes from 'prop-types';
import Draggable from 'react-draggable';
import styles from './Window.scss';
import Icon from '../Icon/Icon';
import Button from '../Input/Button/Button';
import { excludeKeys } from '../../../utils/helpers';

const getWindowTypeClass = (type) => {
  let className;
  switch(type) {
    case 'small':
      className='small'
      break;
    
    case 'medium':
      className=''
      break;
    
    case 'large':
      className='large'
      break;

  }
  return className;
}
const Window = (props) => {
  const { children, title, closeCallback, className, size, position, type } = props;
  return (
    <Draggable defaultPosition={position} handle=".header">
      <section
        className={`${styles.window} ${className} ${getWindowTypeClass(type)}`}
        style={{
          width: size.width,
          height: size.height,
        }}
        {...excludeKeys(props, 'children title closeCallback className position')}
      >
        <header className="header">
          <div className={styles.title}>
            { title }
          </div>
          { closeCallback && (
            <Button onClick={closeCallback} transparent small>
              <Icon icon="close" className="small" />
            </Button>
          )}
        </header>
        <section className={styles.filler}>
          { children }
        </section>
      </section>
    </Draggable>
  );
}

Window.propTypes = {
  children: PropTypes.node,
  closeCallback: PropTypes.func,
  className: PropTypes.string,
  position: PropTypes.shape({
    x: PropTypes.number,
    y: PropTypes.number,
  }),
  size: PropTypes.shape({
    height: PropTypes.number,
    width: PropTypes.number,
  }),
  type: PropTypes.oneOf(['small', 'medium', 'large']),
  title: PropTypes.string,
};

Window.defaultProps = {
  children: '',
  closeCallback: null,
  className: '',
  position: { x: 10, y: 10 },
  size: { height: 'auto', width: '300px' },
  type: "medium",
  title: 'Window',
};

export default Window;
